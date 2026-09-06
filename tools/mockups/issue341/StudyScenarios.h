#pragma once

template<class T> static T* studyControl(Mockup& w,QString name) {
    for(auto item:w.findChildren<T*>())if(item->accessibleName()==name)return item;return nullptr;
}
static QPushButton* studyButton(Mockup& w,QString title) {
    for(auto item:w.findChildren<QPushButton*>())if(item->text()==title)return item;return nullptr;
}
static int runStudyScenarios(Mockup& w,const QStringList& args) {
    if(args.contains("--test-study")) {
        int failures=0,checks=0;QJsonArray results;auto check=[&](bool ok,QString name){++checks;results.append(QJsonObject{{"pass",ok},{"check",name}});QTextStream(stdout)<<(ok?"PASS ":"FAIL ")<<name<<Qt::endl;if(!ok)++failures;};
        for(QString theme:{"soft-light","studio-dark"}) {
            w.documents.clear();w.documentDirty.clear();w.undoByFile.clear();w.redoByFile.clear();w.themeName=theme;w.t=themeFor(theme);w.resize(1600,1000);w.beginStudy();w.show();qApp->processEvents();
            check(w.commands.size()==13,theme+" keeps original 13 config lines");check(w.findChildren<QMenuBar*>().size()==1,theme+" includes actual menu bar");
            check(studyControl<QTabBar>(w,"설정 파일 탭")!=nullptr,theme+" includes file tabs");
            w.showPicker(3);qApp->processEvents();check(w.studyOverlay && w.pickerSearch,theme+" picker opens inside editor");
            w.pickerSearch->setText("no such filter 999");check(!w.pickerAdd->isEnabled(),theme+" empty search cannot insert");
            w.pickerSearch->setText("VST");check(w.pickerList->count()==1,theme+" search resolves one VST template");
            QTest::mouseClick(w.pickerAdd,Qt::LeftButton);qApp->processEvents();check(w.commands.size()==14 && w.commands[3].kind=="VST" && w.commands[4].kind=="LS",theme+" inserts before selected row");
            w.undoStudy();check(w.commands.size()==13 && w.commands[3].kind=="LS",theme+" undo restores order");w.undoStudy(true);check(w.commands.size()==14 && w.commands[3].kind=="VST",theme+" redo restores insertion");w.deleteStudyCommand();check(w.commands.size()==13,theme+" delete removes selected command");
            w.loadStudyFile("effects.txt");qApp->processEvents();check(w.commands[3].missing,theme+" reference example begins missing");
            auto bus=studyControl<QComboBox>(w,"VST 입력 버스");check(bus!=nullptr,theme+" VST input bus selector exists");if(bus)bus->setCurrentText("Mono");check(w.commands[2].inputBus=="Mono",theme+" bus request updates model");
            check(raw(w.commands[2]).contains("Input Mono Output Stereo"),theme+" raw preview reflects VST bus request");
            w.showFileChoice(3);qApp->processEvents();auto replacement=studyButton(w,"Presets\\headphones-fixed.txt");check(replacement!=nullptr,theme+" recovery offers fixture replacement");if(replacement)QTest::mouseClick(replacement,Qt::LeftButton);qApp->processEvents();check(!w.commands[3].missing && w.commands[3].detail.contains("fixed"),theme+" recovery clears only mock missing state");
            w.loadStudyFile("routing.txt");qApp->processEvents();auto coefficient=studyControl<QDoubleSpinBox>(w,"R 입력 계수");check(coefficient!=nullptr,theme+" route coefficient editor exists");if(coefficient)coefficient->setValue(.25);check(w.commands[3].q==.25,theme+" routing edit updates coefficient");w.undoStudy();check(w.commands[3].q==.5,theme+" routing undo restores coefficient");
            w.loadStudyFile("graphic.txt");qApp->processEvents();auto gain=studyControl<QDoubleSpinBox>(w,"선택 지점 게인");check(gain!=nullptr,theme+" graphic EQ has numeric editor");if(gain)gain->setValue(-3);check(w.commands[2].points[1].y()==-3,theme+" graphic EQ point changes");w.graphicPlot->setFocus();QTest::keyClick(w.graphicPlot,Qt::Key_Up);check(w.commands[2].points[1].y()>-3,theme+" graphic EQ supports keyboard");
            w.loadStudyFile("effects.txt");check(!w.commands[3].missing && w.commands[2].inputBus=="Mono",theme+" file tabs retain independent edits");
            w.showAnalysisSettings();qApp->processEvents();check(w.studyOverlay!=nullptr,theme+" analysis settings open");w.closeStudyOverlay();
            w.resize(1280,900);qApp->processEvents();check(w.scroll->horizontalScrollBar()->maximum()==0,theme+" no horizontal scroll at 1280");
            bool inside=true;for(auto combo:w.findChildren<QComboBox*>())if(combo->isVisibleTo(&w)){QRect bounds(combo->mapTo(&w,QPoint()),combo->size());inside=inside && bounds.left()>=0 && bounds.right()<w.width();}check(inside,theme+" visible combo boxes fit actual window bounds");
            check(w.dirty,theme+" changed flag survives switching files");
            w.commands[2].detail="Plugins\\아주 긴 플러그인 파일 이름과 영문 Stereo Room Correction Processor For Studio Monitors.vst3";w.rebuild();qApp->processEvents();bool namesFit=true;for(auto child:w.findChildren<QLabel*>("studyReferenceName"))namesFit=namesFit && child->width()<=420;check(namesFit,theme+" long reference identities have bounded elision");
            w.showPicker(2);qApp->processEvents();auto panel=w.studyOverlay->findChild<QWidget*>("studyOverlayPanel");check(panel && w.rect().contains(panel->geometry()),theme+" picker fits compact window");w.closeStudyOverlay();
            int widgets=w.findChildren<QWidget*>().size();for(int i=0;i<5;++i){w.rebuild();qApp->processEvents();}check(w.findChildren<QWidget*>().size()==widgets,theme+" rebuild does not accumulate widgets");
        }
        int reportArg=args.indexOf("--report");if(reportArg>=0 && reportArg+1<args.size()){QFile report(args[reportArg+1]);if(!report.open(QIODevice::WriteOnly))return 2;report.write(QJsonDocument(QJsonObject{{"qt",qVersion()},{"checks",checks},{"failures",failures},{"results",results}}).toJson());}
        QTextStream(stdout)<<"STUDY RESULT "<<checks<<" checks, "<<failures<<" failures"<<Qt::endl;return failures?1:0;
    }
    int arg=args.indexOf("--render-study");if(arg<0 || arg+1>=args.size())return 2;QString output=args[arg+1];QDir().mkpath(output);bool ok=true;
    for(QString theme:{"soft-light","studio-dark"}) {
        w.documents.clear();w.documentDirty.clear();w.undoByFile.clear();w.redoByFile.clear();w.themeName=theme;w.t=themeFor(theme);w.resize(1600,1000);w.analysisMetric="크기";
        for(QString file:{"config.txt","effects.txt","routing.txt","graphic.txt"}){w.beginStudy(file);QString scene=file.section('.',0,0);ok=save(w,output,theme+"-"+scene)&&ok;}
        w.beginStudy();w.show();qApp->processEvents();w.showPicker(w.selected);ok=save(w,output,theme+"-picker")&&ok;w.closeStudyOverlay();
        w.beginStudy("effects.txt");w.showAnalysisSettings();ok=save(w,output,theme+"-analysis-settings")&&ok;w.closeStudyOverlay();
        w.beginStudy("routing.txt");ok=save(w,output,theme+"-routing-1280",QSize(1280,900))&&ok;
        w.beginStudy("effects.txt");ok=save(w,output,theme+"-effects-1280",QSize(1280,900))&&ok;
        w.commands[3].missing=false;w.commands[3].detail="Presets\\headphones-fixed.txt";w.rebuild();ok=save(w,output,theme+"-recovered")&&ok;
        w.commands[2].detail="Plugins\\아주 긴 플러그인 파일 이름과 영문 Stereo Room Correction Processor For Studio Monitors.vst3";w.rebuild();ok=save(w,output,theme+"-long-reference",QSize(1280,900))&&ok;
    }
    for(QString theme:{"soft-dark","studio-light"}){w.documents.clear();w.themeName=theme;w.t=themeFor(theme);w.beginStudy("effects.txt");ok=save(w,output,theme+"-effects")&&ok;}
    QTextStream(stdout)<<"STUDY RENDER "<<(ok?"PASS ":"FAIL ")<<output<<Qt::endl;return ok?0:1;
}
