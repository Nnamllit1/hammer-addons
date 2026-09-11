#include "hammer_editor.h"
#include "hammer_build.h"
#include "hammer_jobs.h"
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
        if(lower.find("error")!=std::string::npos || lower.find("warning")!=std::string::npos ||
           lower.find("failed")!=std::string::npos || lower.find("fatal")!=std::string::npos) {
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
    summary+="\nKeyword matches can include harmless messages (such as '0 errors'). This tool does not validate assets or infer the cause.";
    std::snprintf(result,capacity,"%s",summary.c_str());return 1;
}
static uint64_t latestStream;
static int HA_CALL observe_build(void*,const HA_InteractionV1* event,char*,size_t) {
    const auto* output=HA_GetBuildOutput(event);if(!output)return HA_ERROR;
    const bool closed=std::string(event->phase)=="build.closed";
    if(closed && latestStream!=output->stream_id)return HA_HANDLED;
    latestStream=output->stream_id;
    if(running){jobs->cancel(host->context,running);running=0;}
    const std::string text(output->text);
    size_t lines=0,matched=0,shown=0,begin=0;
    std::string findings;
    while(begin<text.size()) {
        auto end=text.find('\n',begin);if(end==std::string::npos)end=text.size();
        auto line=text.substr(begin,end-begin);begin=end+1;++lines;
        auto lower=line;
        std::transform(lower.begin(),lower.end(),lower.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
        if(lower.find("error")!=std::string::npos || lower.find("warning")!=std::string::npos ||
           lower.find("failed")!=std::string::npos || lower.find("fatal")!=std::string::npos) {
            ++matched;
            if(shown<10) {
                if(line.size()>240) {
                    size_t cut=240;while(cut && (static_cast<unsigned char>(line[cut])&0xc0)==0x80)--cut;
                    line.resize(cut);line+="...";
                }
                findings+="Line "+std::to_string(lines)+": "+line+"\n";++shown;
            }
        }
    }
    std::string report=std::to_string(lines)+" displayed lines; "+std::to_string(matched)+" diagnostic candidates.\n";
    if(output->flags&HA_BUILD_TRUNCATED)report+="Only the latest output tail is included; earlier text was omitted. Line numbers refer to this tail.\n";
    report+=findings.empty()?"No recognized diagnostic keywords in this snapshot.\n":"\n"+findings;
    report+="\nKeyword matches can include harmless messages such as '0 errors'. Check Hammer's build output for full context; this report does not determine build success.";
    std::string status=closed?"Build dialog closed. Last captured output: ":"Following Hammer build output: ";
    status+=output->title;
    HA_SetPanelText(host,panel,"status",status.c_str());
    HA_SetPanelText(host,panel,"report",report.c_str());
    return HA_HANDLED;
}
static int HA_CALL interact(void*,const HA_InteractionV1* event,char* response,size_t capacity) {
    if(std::string(event->phase)=="import") {
        if(running) {std::snprintf(response,capacity,"A scan is already running. Cancel it from Build log report first.");return HA_ERROR;}
        const HA_JobV1 request{sizeof(request),event->value,scan,notice,0};
        running=jobs->submit(host->context,&request);
        if(!running) {std::snprintf(response,capacity,"Cannot start scan: host busy or job limit reached. Try again.");return HA_ERROR;}
        HA_SetPanelText(host,panel,"report","");
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
        if(!jobs->post(host->context,notice,"Build a map normally in Hammer (F9). This report follows its build output automatically. Inspect build log... is optional for saved files.",0))return HA_ERROR;
    }
    return HA_HANDLED;
}
static int HA_CALL load(const HA_HostV1* api) {
    host=api;jobs=HA_GetJobs(api);const auto* ext=HA_GetExtensions(api);
    if(!ext || !jobs)return 0;
    const HA_ControlV1 controls[]{
        {sizeof(HA_ControlV1),HA_LABEL,"status","Status","Waiting for Hammer build output. Build a map normally (F9).",nullptr},
        {sizeof(HA_ControlV1),HA_TEXT_VIEW,"report","Diagnostic candidates","",nullptr},
        {sizeof(HA_ControlV1),HA_BUTTON,"cancel","Cancel scan",nullptr,nullptr},
        {sizeof(HA_ControlV1),HA_BUTTON,"help","How to use",nullptr,nullptr}
    };
    const HA_ContributionV1 view{sizeof(view),HA_PANEL,"report","all","Build log report","","",controls,4,interact,nullptr};
    panel=ext->register_contribution(host->context,&view);
    const HA_ContributionV1 importer{sizeof(importer),HA_IMPORTER,"inspect","all","Inspect build log...","","log,txt",nullptr,0,interact,nullptr};
    const HA_ContributionV1 observer{sizeof(observer),HA_BUILD_OBSERVER,"automatic","hammer","Automatic build output","","",nullptr,0,observe_build,nullptr};
    return panel && ext->register_contribution(host->context,&importer) && ext->register_contribution(host->context,&observer);
}
extern "C" HA_EXPORT const HA_AddonV1* HA_CALL HA_Query(uint32_t abi) {
    static const HA_AddonV1 addon{sizeof(addon),HA_ABI_VERSION,"compile_report",
        HA_CAP_BUILD_OUTPUT|HA_CAP_UI|HA_CAP_IMPORTERS|HA_CAP_LIVE_PANELS|HA_CAP_JOBS|HA_CAP_EDITOR_QUEUE,load,nullptr,nullptr};
    return abi==HA_ABI_VERSION ? &addon : nullptr;
}
