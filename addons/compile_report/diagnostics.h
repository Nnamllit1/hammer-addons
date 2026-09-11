#pragma once
#include <algorithm>
#include <cstdint>
#include <cctype>
#include <cstdio>
#include <deque>
#include <map>
#include <regex>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace diagnostics {
inline std::string lower(std::string value) {
    std::transform(value.begin(),value.end(),value.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});return value;
}
inline std::string clip(std::string value,size_t limit) {
    if(value.size()<=limit)return value;
    auto end=limit;while(end && (static_cast<unsigned char>(value[end])&0xc0)==0x80)--end;
    value.resize(end);return value+"...";
}
inline std::string cell(std::string value) {
    for(auto& c:value)if(c=='\t' || c=='\n' || c=='\r')c=' ';
    return value;
}
inline std::string identity(const std::string& value) {
    uint64_t hash=14695981039346656037ull;
    for(unsigned char c:value){hash^=c;hash*=1099511628211ull;}
    char out[32]{};std::snprintf(out,sizeof(out),"d%016llx",static_cast<unsigned long long>(hash));return out;
}
struct Issue {
    std::string key,severity,message,explanation,asset;
    size_t line=0,count=1;
};
struct Report {
    std::vector<Issue> issues;
    std::string start,elapsed;
    size_t lines=0,matched=0,omitted=0;
    bool truncated=false,ended=false,hasSummary=false;
    unsigned failed=0;
    bool comparable() const {return !truncated && !omitted && !start.empty() && ended && hasSummary;}
    std::string result() const {
        if(!ended)return "In progress / outcome unknown";
        if(!hasSummary)return "Build ended; result unknown";
        return failed ? "Compiler reported failure" : "Compiler reported success";
    }
    std::string render() const {
        std::string text=std::to_string(lines)+" displayed lines; "+std::to_string(issues.size())+" diagnostic candidates ("+std::to_string(matched)+" occurrences).\n"+result();
        if(!elapsed.empty())text+="; elapsed "+elapsed;
        text+="\n";
        if(truncated)text+="Only the latest output tail is included; earlier text was omitted. Line numbers refer to this tail.\n";
        if(omitted)text+="More unique diagnostics were omitted by the report limit.\n";
        for(size_t i=0;i<std::min<size_t>(10,issues.size());++i) {
            const auto& issue=issues[i];text+="Line "+std::to_string(issue.line)+" ("+std::to_string(issue.count)+"x): "+clip(issue.message,220)+"\n";
        }
        text+="\nSelect a problem for its original message and suggested checks. Unrecognized diagnostic wording is a candidate, not a verified cause.";
        return clip(text,3900);
    }
    std::string rows() const {
        std::string out;
        for(const auto& issue:issues) {
            auto row=issue.key+"\t"+issue.severity+"\t"+std::to_string(issue.count)+"\t"+cell(clip(issue.message,180))+"\n";
            if(out.size()+row.size()>4000)break;out+=row;
        }
        return out;
    }
};
inline Report analyze(std::string_view text,bool truncated=false) {
    Report report;report.truncated=truncated;
    static const std::regex summary(R"(^OK:\s*[0-9]+ compiled,\s*([0-9]+) failed,.*$)",std::regex::icase);
    static const std::regex zero(R"(\b0\s+(errors?|warnings?|failed)\b)",std::regex::icase);
    static const std::regex diagnostic(R"(\b(error|warning|failed|fatal)\b)",std::regex::icase);
    static const std::regex start(R"(^Start build: ([0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2})\s*$)");
    static const std::regex end(R"(^End build: [0-9T:\-]+, elapsed time (.+)\.?$)");
    std::map<std::string,size_t> unique;
    size_t begin=0;
    while(begin<text.size()) {
        const auto stop=text.find('\n',begin);
        std::string line(text.substr(begin,stop==std::string_view::npos?stop:stop-begin));
        begin=stop==std::string_view::npos?text.size():stop+1;++report.lines;
        if(line.size()>8192){line=clip(line,8192);report.truncated=true;}
        if(!line.empty() && line.back()=='\r')line.pop_back();
        std::smatch match;
        if(std::regex_match(line,match,start)){report.start=match[1].str();continue;}
        if(std::regex_match(line,match,end)){report.ended=true;report.elapsed=clip(match[1].str(),80);continue;}
        if(std::regex_match(line,match,summary)) {
            try {report.failed=static_cast<unsigned>(std::min<unsigned long>(1000000,std::stoul(match[1].str())));report.hasSummary=true;}catch(...){}
            // Counts are metadata, never a diagnostic saying that "0 failed" is an error.
            continue;
        }
        if(!std::regex_search(std::regex_replace(line,zero,""),diagnostic))continue;
        ++report.matched;
        auto found=unique.find(line);
        if(found!=unique.end()){++report.issues[found->second].count;continue;}
        if(report.issues.size()==64){++report.omitted;continue;}
        Issue issue;issue.key=identity(line);issue.line=report.lines;issue.message=clip(line,4096);
        issue.severity=lower(line).find("warning")!=std::string::npos?"Warning":"Candidate";
        issue.explanation="Unrecognized diagnostic. Check the surrounding lines in Hammer's build output before deciding what to change.";
        const auto normalized=lower(line);
        const auto resource=normalized.find("failed loading resource \"");
        if(resource!=std::string::npos) {
            const auto first=line.find('"',resource),last=line.find('"',first+1);
            if(last!=std::string::npos)issue.asset=line.substr(first+1,last-first-1);
            issue.severity="Resource error";
            issue.explanation="The compiler reported that it could not load this resource. Check the spelling, source asset, and whether its package is mounted. A project-local lookup does not check VPKs and cannot prove the resource is absent.";
        } else if(normalized.find("unknown sound operator attribute")!=std::string::npos) {
            issue.severity="Sound error";
            issue.explanation="The compiler did not recognize a sound-operator attribute. Check the named operator and its attributes against the installed tools version; this message alone does not establish a map build failure.";
        } else if(normalized.starts_with("fatal") || normalized.starts_with("error:"))issue.severity="Error";
        // Resolve the extremely unlikely hash collision without merging different messages.
        auto key=issue.key;unsigned collision=0;
        while(std::any_of(report.issues.begin(),report.issues.end(),[&](const auto& i){return i.key==issue.key;}))issue.key=key+"_"+std::to_string(++collision);
        unique.emplace(line,report.issues.size());report.issues.push_back(std::move(issue));
    }
    return report;
}
struct Build {uint64_t window=0,stream=0;std::string title,token;Report report;bool saved=false;};
class History {
    std::map<uint64_t,Build> active_;
    std::deque<Build> finished_;
    std::string comparison_="No comparable previous build in this session.";
    void save(Build& build) {
        if(build.saved || !build.report.lines)return;
        comparison_="No previous captured build for this editor/map title.";
        for(const auto& previous:finished_)if(previous.window==build.window && previous.title==build.title) {
            comparison_="Comparison unavailable: this build or its previous capture is incomplete.";
            if(previous.report.comparable() && build.report.comparable()) {
                std::set<std::string> before,after;
                for(const auto& i:previous.report.issues)before.insert(i.message);
                for(const auto& i:build.report.issues)after.insert(i.message);
                size_t added=0,removed=0;
                for(const auto& m:after)added+=!before.contains(m);
                for(const auto& m:before)removed+=!after.contains(m);
                comparison_=std::to_string(added)+" new, "+std::to_string(removed)+" resolved diagnostic messages since the previous captured build.";
            }
            break;
        }
        if(finished_.empty())comparison_="No previous captured build in this session.";
        build.saved=true;finished_.push_front(build);if(finished_.size()>8)finished_.pop_back();
    }
public:
    void update(uint64_t window,uint64_t stream,const std::string& title,const Report& report,bool closed) {
        auto found=active_.find(stream);
        if(found==active_.end()) {
            if(active_.size()>=16)active_.erase(active_.begin());
            found=active_.emplace(stream,Build{window,stream,title,{},Report{},false}).first;
        }
        auto& build=found->second;
        if((!report.start.empty() && build.token!=report.start) || (!report.lines && build.report.lines) || build.title!=title) {
            save(build);build=Build{window,stream,title,{},Report{},false};
        }
        if(!report.start.empty())build.token=report.start;
        build.report=report;
        if(report.ended || closed)save(build);
        if(closed)active_.erase(found);
    }
    const std::deque<Build>& builds() const{return finished_;}
    std::string render() const {
        std::string out="Last "+std::to_string(finished_.size())+" captured builds (this session, maximum 8).\n"+comparison_+"\n";
        for(const auto& build:finished_)out+="\n"+clip(build.title,160)+" | "+(build.token.empty()?"start not captured":build.token)+"\n"+
            build.report.result()+" | "+std::to_string(build.report.issues.size())+" unique diagnostics"+
            (build.report.comparable()?"":" | incomplete capture")+(build.report.elapsed.empty()?"":" | "+build.report.elapsed)+"\n";
        return clip(out,3900);
    }
};
}
