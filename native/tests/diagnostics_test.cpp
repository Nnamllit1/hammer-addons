#include "../../addons/compile_report/diagnostics.h"
#include <iostream>
#include <stdexcept>
using namespace diagnostics;
static void check(bool ok,const char* text){if(!ok)throw std::runtime_error(text);}
static std::string build(const char* timestamp,const std::string& messages) {
    return std::string("Start build: ")+timestamp+"\n"+messages+"\nOK: 17 compiled, 0 failed, 1 skipped, 0m:04s\nEnd build: "+timestamp+", elapsed time 0h:00m:05s.044ms\n";
}
int main() {
    try {
        const auto text=build("2026-09-11T12:00:00","Failed loading resource \"materials/missing.vmat_c\" (ERROR_FILEOPEN: File not found)\nwarning repeated\nwarning repeated\n0 errors, 0 warnings");
        auto report=analyze(text);check(report.issues.size()==2 && report.matched==3,"zero counts ignored and repeats grouped");
        check(report.hasSummary && !report.failed && report.ended && report.comparable(),"compiler result recognized separately");
        check(report.issues[0].asset=="materials/missing.vmat_c" && !report.issues[0].explanation.empty(),"resource evidence and explanation");
        check(report.issues[1].count==2 && report.issues[1].line==3,"first line and occurrence count");
        check(analyze("0 errors but failed to write output").issues.size()==1,"zero count does not mask another diagnostic");
        check(analyze("OK: 1 compiled, 2 failed, 0 skipped\n").failed==2,"nonzero failure count preserved");
        check(!analyze("warning only\n").comparable(),"unframed output cannot compare");
        check(!analyze(text,true).comparable(),"truncated output cannot compare");
        auto longLine=analyze(std::string(50000,'x')+"warning");check(longLine.truncated,"overlong line explicitly partial");
        check(analyze("Error: Stack foo, unknown sound operator attribute test").issues[0].severity=="Sound error","known sound diagnostic");
        History history;
        history.update(1,1,"Build Map test.vmap",report,false);check(history.builds().size()==1,"completed build saved once");
        history.update(1,1,"Build Map test.vmap",report,false);check(history.builds().size()==1,"duplicate snapshot not archived twice");
        history.update(1,1,"Build Map test.vmap",analyze(""),false);
        auto next=analyze(build("2026-09-11T12:01:00","warning new"));
        history.update(1,1,"Build Map test.vmap",next,false);
        check(history.builds().size()==2 && history.render().find("1 new, 2 resolved")!=std::string::npos,"new and resolved diagnostics compared");
        history.update(1,1,"Build Map test.vmap",next,true);check(history.builds().size()==2,"closing completed build does not duplicate history");
        history.update(2,2,"Build Map test.vmap",report,false);
        check(history.render().find("No previous captured build for this editor/map title")!=std::string::npos,"different editor window not compared");
        history.update(1,3,"Build Map test.vmap",analyze(build("2026-09-11T12:02:00","warning partial"),true),false);
        check(history.render().find("Comparison unavailable")!=std::string::npos,"partial build not counted as resolved issues");
        for(int i=0;i<12;++i)history.update(1,100+i,"Map "+std::to_string(i),report,false);
        check(history.builds().size()==8 && history.render().size()<4096,"bounded history");
        std::cout<<"Diagnostics/history tests passed\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
