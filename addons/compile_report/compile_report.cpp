#include "hammer_editor.h"
#include "hammer_build.h"
#include "hammer_jobs.h"
#include "hammer_project.h"
#include "diagnostics.h"
#include <windows.h>
#include <shlobj.h>
#include <vector>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

static const HA_HostV1* host;
static const HA_JobsV1* jobs;
static uint64_t panel, running;
static void HA_CALL notice(const HA_JobEventV1* event) {
    if(event->job_id && event->job_id!=running)return;
    if(event->state==HA_JOB_PROGRESS || event->state==HA_JOB_MESSAGE) {
        HA_SetPanelText(host,panel,"status",event->text);return;
    }
    const char* status=event->state==HA_JOB_SUCCEEDED ? "Finished. Open the full log for context." :
        event->state==HA_JOB_CANCELLED ? "Cancelled." : "Could not inspect this log.";
    HA_SetPanelText(host,panel,"status",status);
    HA_SetPanelText(host,panel,"report",event->text);
    running=0;
}
// This worker owns its input/results. It never reads the add-on's UI globals.
static int HA_CALL scan(const HA_JobContextV1* job,const char* input,char* result,size_t capacity) {
    namespace fs=std::filesystem;
    const auto path=fs::path(std::u8string(reinterpret_cast<const char8_t*>(input)));
    std::error_code error;
    const auto bytes=fs::file_size(path,error);
    if(error || !fs::is_regular_file(path,error) || bytes>32*1024*1024) {
        std::snprintf(result,capacity,"Choose a readable text log up to 32 MiB.");return 0;
    }
    std::ifstream in(path,std::ios::binary);
    if(!in) {std::snprintf(result,capacity,"Cannot open the selected log.");return 0;}
    job->post(job->context,"Reading the selected log in the background...");
    size_t consumed=0, lines=0, matched=0, shown=0, overlong=0;
    std::string findings,line;
    // Bound each line as well as the whole input; a growing file cannot scan forever.
    while(in && consumed<=32*1024*1024) {
        if(job->cancelled(job->context))return 0;
        line.clear();bool clipped=false;char ch;
        while(in.get(ch)) {
            if(++consumed>32*1024*1024) {
                std::snprintf(result,capacity,"Log grew past the 32 MiB scan limit; select a completed log.");return 0;
            }
            if(ch==0) {std::snprintf(result,capacity,"Binary or UTF-16 log detected. Export a UTF-8/plain-text log.");return 0;}
            if(ch=='\n')break;
            if(line.size()<8192)line+=ch;else clipped=true;
            if((consumed%4096)==0 && job->cancelled(job->context))return 0;
        }
        if(line.empty() && !in && !clipped)break;
        ++lines;if(clipped)++overlong;
        auto lower=line;
        std::transform(lower.begin(),lower.end(),lower.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
        // These are diagnostic candidates, not proof of failure or an inferred fix.
        if(!diagnostics::analyze(line).issues.empty()) {
            ++matched;
            if(shown<10 && findings.size()<2800) {
                if(line.size()>240) {
                    size_t end=240;
                    while(end && (static_cast<unsigned char>(line[end])&0xc0)==0x80)--end;
                    line.resize(end);line+="...";
                }
                findings+="Line "+std::to_string(lines)+": "+line+"\n";++shown;
            }
        }
        if(lines%256==0) {
            const auto progress=static_cast<uint32_t>(std::min<size_t>(99,bytes ? consumed*100/bytes : 0));
            const auto text="Scanning: "+std::to_string(lines)+" lines, "+std::to_string(matched)+" diagnostic candidates";
            job->report(job->context,progress,text.c_str());
        }
    }
    if(in.bad()) {std::snprintf(result,capacity,"I/O error while reading the log; results would be incomplete.");return 0;}
    std::string summary=std::to_string(lines)+" lines scanned; "+std::to_string(matched)+" diagnostic candidates.\n";
    if(matched)summary+="Showing "+std::to_string(shown)+" matching lines. Check surrounding lines in the original log.\n\n"+findings;
    else summary+="No recognized diagnostic keywords. This does not establish that the build succeeded.\n";
    if(overlong)summary+="\n"+std::to_string(overlong)+" long lines were inspected only through their first 8192 bytes.\n";
    summary+="\nThese are diagnostic candidates; this scan does not validate assets or infer the cause.";
    std::snprintf(result,capacity,"%s",summary.c_str());return 1;
}
static const HA_ProjectV1* project;
static uint64_t latestStream;
static diagnostics::Report currentReport;
static diagnostics::History history;
static std::string selected;
static const diagnostics::Issue* selected_issue() {
    for(const auto& issue:currentReport.issues)if(issue.key==selected)return &issue;
    return nullptr;
}
static std::string source_path(const diagnostics::Issue& issue) {
    if(!project || issue.asset.empty())return {};
    auto relative=issue.asset;
    if(relative.ends_with("_c"))relative.resize(relative.size()-2);
    const auto required=project->source_path(host->context,relative.c_str(),nullptr,0);
    if(!required || required>32768)return {};
    std::vector<char> buffer(required);
    if(project->source_path(host->context,relative.c_str(),buffer.data(),buffer.size())!=required)return {};
    return buffer.data();
}
static void show_details() {
    std::string details="Select a problem to see its full message and suggested checks.";
    if(const auto* issue=selected_issue()) {
        details=issue->severity+" | line "+std::to_string(issue->line)+" | "+std::to_string(issue->count)+" occurrence(s)\n\n"+
            diagnostics::clip(issue->message,2000)+"\n\n"+issue->explanation;
        if(!issue->asset.empty()) {
            details+="\n\nAsset: "+issue->asset;
            const auto source=source_path(*issue);
            details+=source.empty()?"\nNo project-local source file resolved. Packaged/base-game assets are not checked.":"\nSource: "+source;
        }
    }
    details=diagnostics::clip(details,3900);HA_SetPanelText(host,panel,"details",details.c_str());
}
static int HA_CALL observe_build(void*,const HA_InteractionV1* event,char*,size_t) {
    const auto* output=HA_GetBuildOutput(event);if(!output)return HA_ERROR;
    const bool closed=std::string(event->phase)=="build.closed";
    const auto report=diagnostics::analyze(output->text,(output->flags&HA_BUILD_TRUNCATED)!=0);
    history.update(output->window_id,output->stream_id,output->title,report,closed);
    HA_SetPanelText(host,panel,"history",history.render().c_str());
    if(closed && latestStream!=output->stream_id)return HA_HANDLED;
    latestStream=output->stream_id;currentReport=report;
    if(running){jobs->cancel(host->context,running);running=0;}
    std::string status=closed?"Build dialog closed: ":"Following Hammer build output: ";status+=output->title;
    HA_SetPanelText(host,panel,"status",status.c_str());
    HA_SetPanelText(host,panel,"report",currentReport.render().c_str());
    HA_SetPanelText(host,panel,"problems",currentReport.rows().c_str());show_details();
    return HA_HANDLED;
}
static bool copy_text(const std::string& text) {
    const auto value=std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(text.c_str()))).wstring();
    const auto window=GetActiveWindow();if(!window || !OpenClipboard(window))return false;
    struct Close {~Close(){CloseClipboard();}} close;
    const size_t bytes=(value.size()+1)*sizeof(wchar_t);
    HGLOBAL memory=GlobalAlloc(GMEM_MOVEABLE,bytes);if(!memory)return false;
    auto* destination=GlobalLock(memory);if(!destination){GlobalFree(memory);return false;}
    memcpy(destination,value.c_str(),bytes);GlobalUnlock(memory);
    if(!EmptyClipboard() || !SetClipboardData(CF_UNICODETEXT,memory)){GlobalFree(memory);return false;}
    return true; // Clipboard owns the allocation after SetClipboardData succeeds.
}
static bool reveal_source(const std::string& source) {
    const auto path=std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(source.c_str())));
    const HRESULT initialized=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    PIDLIST_ABSOLUTE item=nullptr;bool result=false;
    if(SUCCEEDED(SHParseDisplayName(path.c_str(),nullptr,&item,0,nullptr))) {
        result=SUCCEEDED(SHOpenFolderAndSelectItems(item,0,nullptr,0));CoTaskMemFree(item);
    }
    if(SUCCEEDED(initialized))CoUninitialize();return result;
}
static int HA_CALL interact(void*,const HA_InteractionV1* event,char* response,size_t capacity) {
    const std::string control=event->control_id;
    if(control=="problems") {selected=event->value;show_details();return HA_HANDLED;}
    if(control=="copy" || control=="source") {
        const auto* issue=selected_issue();
        if(!issue){std::snprintf(response,capacity,"Select a current problem first.");return HA_ERROR;}
        if(control=="copy") {
            if(!copy_text(issue->asset.empty()?issue->message:issue->asset)) {
                std::snprintf(response,capacity,"Could not open the clipboard. Try again.");return HA_ERROR;
            }
        } else {
            const auto path=source_path(*issue);
            if(path.empty() || !reveal_source(path)) {
                std::snprintf(response,capacity,"No project-local source could be revealed. Packaged assets are not checked.");return HA_ERROR;
            }
        }
        return HA_HANDLED;
    }
    if(std::string(event->phase)=="import") {
        if(running) {std::snprintf(response,capacity,"A scan is already running. Cancel it from Build log report first.");return HA_ERROR;}
        const HA_JobV1 request{sizeof(request),event->value,scan,notice,0};
        running=jobs->submit(host->context,&request);
        if(!running) {std::snprintf(response,capacity,"Cannot start scan: host busy or job limit reached. Try again.");return HA_ERROR;}
        HA_SetPanelText(host,panel,"report","");
        currentReport={};selected.clear();HA_SetPanelText(host,panel,"problems","");show_details();
        HA_SetPanelText(host,panel,"status","Scan started. Results appear in Build log report.");
        std::snprintf(response,capacity,"Scanning. Open Workshop Add-ons > Build log report for results.");return HA_HANDLED;
    }
    if(std::string(event->control_id)=="cancel") {
        if(running) {
            if(!jobs->cancel(host->context,running))return HA_ERROR;
            HA_SetPanelText(host,panel,"status","Cancellation requested...");
        }
    } else {
        // Demonstrates copied, deferred editor work; runs on a later UI tick.
        if(!jobs->post(host->context,notice,"Build normally with F9. Select a Problems row for details, copy its asset path, or reveal its project-local source. History compares complete captured builds in this session. Saved-log scanning is optional.",0))return HA_ERROR;
    }
    return HA_HANDLED;
}
static int HA_CALL load(const HA_HostV1* api) {
    host=api;jobs=HA_GetJobs(api);project=HA_GetProject(api);const auto* ext=HA_GetExtensions(api);
    if(!ext || !jobs || !project)return 0;
    const HA_ControlV1 controls[]{
        {sizeof(HA_ControlV1),HA_LABEL,"status","Status","Waiting for Hammer build output. Build a map normally (F9).",nullptr},
        {sizeof(HA_ControlV1),HA_TEXT_VIEW,"report","Build summary","",nullptr},
        {sizeof(HA_ControlV1),HA_TABLE,"problems","Problems","","Severity\tCount\tMessage"},
        {sizeof(HA_ControlV1),HA_TEXT_VIEW,"details","Problem details","Select a problem for details.",nullptr},
        {sizeof(HA_ControlV1),HA_BUTTON,"copy","Copy asset path / diagnostic",nullptr,nullptr},
        {sizeof(HA_ControlV1),HA_BUTTON,"source","Reveal project source",nullptr,nullptr},
        {sizeof(HA_ControlV1),HA_TEXT_VIEW,"history","Build history","No captured builds yet.",nullptr},
        {sizeof(HA_ControlV1),HA_BUTTON,"cancel","Cancel scan",nullptr,nullptr},
        {sizeof(HA_ControlV1),HA_BUTTON,"help","How to use",nullptr,nullptr}
    };
    const HA_ContributionV1 view{sizeof(view),HA_PANEL,"report","all","Build log report","","",controls,static_cast<uint32_t>(std::size(controls)),interact,nullptr};
    panel=ext->register_contribution(host->context,&view);
    const HA_ContributionV1 importer{sizeof(importer),HA_IMPORTER,"inspect","all","Inspect build log...","","log,txt",nullptr,0,interact,nullptr};
    const HA_ContributionV1 observer{sizeof(observer),HA_BUILD_OBSERVER,"automatic","hammer","Automatic build output","","",nullptr,0,observe_build,nullptr};
    return panel && ext->register_contribution(host->context,&importer) && ext->register_contribution(host->context,&observer);
}
extern "C" HA_EXPORT const HA_AddonV1* HA_CALL HA_Query(uint32_t abi) {
    static const HA_AddonV1 addon{sizeof(addon),HA_ABI_VERSION,"compile_report",
        HA_CAP_TABLES|HA_CAP_PROJECT_CONTEXT|HA_CAP_BUILD_OUTPUT|HA_CAP_UI|HA_CAP_IMPORTERS|HA_CAP_LIVE_PANELS|HA_CAP_JOBS|HA_CAP_EDITOR_QUEUE,load,nullptr,nullptr};
    return abi==HA_ABI_VERSION ? &addon : nullptr;
}
