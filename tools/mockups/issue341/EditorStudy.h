#pragma once

static QString studyFingerprint(const QVector<Command>& commands) {
    QStringList lines;for(const auto& c:commands){QString s=c.kind+"|"+c.title+"|"+c.detail+"|"+QString::number(c.hz,'g',16)+"|"+QString::number(c.gain,'g',16)+"|"+QString::number(c.q,'g',16)+"|"+QString::number(c.on)+QString::number(c.missing)+c.inputBus+c.outputBus;for(auto p:c.points)s+=QString::number(p.x(),'g',16)+","+QString::number(p.y(),'g',16)+";";lines<<s;}return lines.join('\n');
}
static QVector<Command> studyTemplates() {
    return {{"프리앰프","PRE",0,0,0,{}},{"피킹 필터","PK",100,0,10,{}},{"하이패스 필터","HP",100,0,.707,{}},{"로우셸프 필터","LS",100,0,.707,{}},
        {"컨볼루션","CONV",0,0,0,"IRs\\new-response.wav",true,true},{"VST 플러그인","VST",0,0,0,"Plugins\\new-plugin.vst3",true,true},
        {"채널 복사","COPY",0,.5,.5,"L = 0.5 × L + 0.5 × R"},{"파일 포함","INCLUDE",0,0,0,"Presets\\new-preset.txt",true,true},
        {"채널","CH",0,0,0,"L · R"},{"지연","DELAY",0,0,0,"10 ms"},{"장치","DEV",0,0,0,"CABLE Input"},{"스테이지","STAGE",0,0,0,"post-mix"}};
}
static QString studyCommandIcon(QString kind) {
    const QMap<QString,QString> names={{"PRE","preamp-gain"},{"PK","eq-peaking"},{"HP","eq-highpass"},{"LS","eq-lowshelf"},{"CONV","waveform"},{"VST","plugin"},{"COPY","route-channels"},{"INCLUDE","file-include"},{"CH","channel-select"},{"DEV","device-speaker"},{"DELAY","delay-clock"},{"STAGE","stage-chain"},{"GEQ","graphic-eq"}};
    return names.value(kind,"comment-bubble");
}
QStringList Mockup::studyFiles() const {return {"config.txt","effects.txt","routing.txt","graphic.txt"};}
void Mockup::beginStudy(QString file) {
    study=true;
    if(documents.isEmpty()) {
        documents["config.txt"]=fixture();
        documents["effects.txt"]={{"채널","CH",0,0,0,"L · R"},{"컨볼루션","CONV",0,0,0,"IRs\\nearfield.wav"},{"VST 플러그인","VST",0,0,0,"Plugins\\Room Processor.vst3"},{"파일 포함","INCLUDE",0,0,0,"Presets\\headphones.txt",true,true}};
        documents["routing.txt"]={{"장치","DEV",0,0,0,"CABLE Input"},{"스테이지","STAGE",0,0,0,"post-mix"},{"채널","CH",0,0,0,"L · R"},{"채널 복사","COPY",0,.5,.5,"L = 0.5 × L + 0.5 × R"},{"지연","DELAY",0,0,0,"10 ms"}};
        Command graphic={"그래픽 EQ","GEQ",0,0,0,"가변 대역 · 4개 지점"};graphic.points={{20,0},{100,3},{1000,-2},{10000,4}};
        documents["graphic.txt"]={{"채널","CH",0,0,0,"L · R"},{"프리앰프","PRE",0,-2,0,{}},graphic,{"주석","COMMENT",0,0,0,"목업용 가변 대역 예제"}};
    }
    activeFile=file;commands=documents.value(file,fixture());selected=qMin(int(commands.size()-1),(file=="graphic.txt" || file=="config.txt")?2:3);expandedStudyRows=file=="effects.txt"?QSet<int>{2,3}:QSet<int>{selected};expanded=true;code=false;dirty=documentDirty.value(file,false);studyCommitted=commands;rebuild();
}
void Mockup::loadStudyFile(QString file) {
    if(file==activeFile)return;recordStudyChange();documents[activeFile]=commands;beginStudy(file);
}
void Mockup::recordStudyChange() {
    if(!study)return;
    if(!studyCommitted.isEmpty() && studyFingerprint(studyCommitted)!=studyFingerprint(commands)) {undoByFile[activeFile].append(studyCommitted);redoByFile[activeFile].clear();dirty=true;}
    studyCommitted=commands;documents[activeFile]=commands;documentDirty[activeFile]=dirty;
    if(auto b=findChild<QPushButton*>("studyUndoButton"))b->setEnabled(!undoByFile[activeFile].isEmpty());
    if(auto b=findChild<QPushButton*>("studyRedoButton"))b->setEnabled(!redoByFile[activeFile].isEmpty());
    if(undoAction)undoAction->setEnabled(!undoByFile[activeFile].isEmpty());
    if(redoAction)redoAction->setEnabled(!redoByFile[activeFile].isEmpty());
}
void Mockup::undoStudy(bool redo) {
    auto& from=redo?redoByFile[activeFile]:undoByFile[activeFile];auto& to=redo?undoByFile[activeFile]:redoByFile[activeFile];
    if(from.isEmpty())return;to.append(commands);commands=from.takeLast();studyCommitted=commands;selected=qBound(0,selected,int(commands.size()-1));expandedStudyRows={selected};dirty=true;rebuild();
}
QIcon Mockup::studyIcon(QString name,QColor tint) const {
    QSvgRenderer svg(repoRoot+"/Editor/icons/modern/"+name+".svg");QPixmap pix(48,48);pix.fill(Qt::transparent);QPainter p(&pix);svg.render(&p);p.setCompositionMode(QPainter::CompositionMode_SourceIn);p.fillRect(pix.rect(),tint);p.end();pix.setDevicePixelRatio(2);return QIcon(pix);
}
QPushButton* Mockup::iconButton(QString icon,QString title,std::function<void()> callback) {
    auto b=button(t.soft?title:QString(),callback);b->setIcon(studyIcon(icon,t.ink));b->setIconSize(QSize(18,18));b->setAccessibleName(title);b->setToolTip(title+" · 목업 내부 동작");if(!t.soft)b->setFixedWidth(42);return b;
}
void Mockup::styleStudy() {
    QString sheet=QString(R"(
        QMenuBar {background:transparent;color:%1;spacing:8px;font-family:'EAPO Sans KR';font-size:14px;}
        QMenuBar::item {padding:7px 10px;}
        QMenuBar::item:selected {background:%2;border-radius:6px;}
        QMenu {background:%3;color:%1;border:1px solid %4;padding:6px;}
        QMenu::item {padding:9px 28px 9px 16px;}
        QMenu::item:selected {background:%2;}
        QTabBar::tab {background:transparent;color:%5;padding:10px 22px;border-bottom:2px solid transparent;min-width:95px;}
        QTabBar::tab:selected {color:%1;background:%3;border-bottom:2px solid %6;}
        QTabBar::tab:hover {background:%2;}
        QLineEdit,QListWidget,QPlainTextEdit {background:%3;color:%1;border:1px solid %4;border-radius:%7px;padding:8px;selection-background-color:%2;}
        QLineEdit:focus {border:2px solid %6;}
        QAbstractSpinBox QLineEdit {background:transparent;border:0;border-radius:0;padding:0;}
        QAbstractSpinBox QLineEdit:focus {background:transparent;border:0;}
        QCheckBox {color:%1;spacing:8px;min-height:36px;}
        QTableWidget {background:%3;color:%1;border:0;gridline-color:%4;selection-background-color:%2;}
        QHeaderView::section {background:%3;color:%5;border:0;padding:6px;}
        QLabel[warning="true"] {color:%8;}
        QComboBox {min-height:22px;}
        QPushButton[primary="true"] {background:%2;color:%6;border:1px solid %6;}
    )").arg(t.ink.name(),t.selected.name(),t.surface.name(),t.line.name(),t.muted.name(),t.accent.name(),QString::number(t.soft?16:6),t.warning.name());
    qApp->setStyleSheet(qApp->styleSheet()+sheet);
}
QWidget* Mockup::studyChrome() {
    auto chrome=new QWidget;auto root=new QVBoxLayout(chrome);root->setContentsMargins(0,0,0,0);root->setSpacing(t.soft?6:2);
    auto title=new QWidget;auto titleL=horizontal(title);title->setFixedHeight(t.soft?38:32);auto brand=label("Equalizer APO XT",false,16);brand->setFont(face(16,true));titleL->addWidget(brand);titleL->addWidget(label(t.name+" · 구성 편집기",true,13));titleL->addStretch();titleL->addWidget(label("통합 목업 · 오디오 / 파일 저장 미연결",true,12));
    auto skins=new StudyComboBox(&t);skins->addItems({"Soft · 라이트","Studio · 다크","Soft · 다크","Studio · 라이트"});QStringList keys={"soft-light","studio-dark","soft-dark","studio-light"};skins->setCurrentIndex(keys.indexOf(themeName));skins->setAccessibleName("스킨 목업 선택");titleL->addWidget(skins);connect(skins,&QComboBox::currentIndexChanged,this,[this,keys](int n){QTimer::singleShot(0,this,[this,keys,n]{themeName=keys[n];t=themeFor(themeName);rebuild();});});root->addWidget(title);
    auto menus=new QMenuBar;menus->setNativeMenuBar(false);
    auto file=menus->addMenu("파일");file->addAction("새 예제 문서",this,[this]{documents["untitled.txt"]={{"주석","COMMENT",0,0,0,"새 목업 문서"}};loadStudyFile("untitled.txt");});file->addAction("열기…",this,[this]{showFileChoice();});file->addAction("저장 · 메모리만",this,[this]{dirty=false;documentDirty[activeFile]=false;if(status)status->setText("저장됨 · 목업");});
    auto edit=menus->addMenu("편집");undoAction=edit->addAction("실행 취소",this,[this]{undoStudy();});undoAction->setShortcut(QKeySequence::Undo);undoAction->setEnabled(!undoByFile[activeFile].isEmpty());redoAction=edit->addAction("다시 실행",this,[this]{undoStudy(true);});redoAction->setShortcut(QKeySequence::Redo);redoAction->setEnabled(!redoByFile[activeFile].isEmpty());edit->addSeparator();edit->addAction("선택 명령 앞에 삽입…",this,[this]{showPicker(selected);});edit->addAction("선택 명령 삭제",this,[this]{deleteStudyCommand();});
    auto view=menus->addMenu("보기");auto graph=view->addAction("분석 패널");graph->setCheckable(true);graph->setChecked(analysisVisible);connect(graph,&QAction::toggled,this,[this](bool v){analysisVisible=v;QTimer::singleShot(0,this,[this]{rebuild();});});view->addAction("분석 설정…",this,[this]{showAnalysisSettings();});
    auto settings=menus->addMenu("설정");auto apo=settings->addAction("APO 설치 / 장치 선택…");apo->setEnabled(false);apo->setToolTip("별도 DeviceSelector 앱은 이번 목업 범위 밖입니다.");settings->addAction("이 목업의 범위",this,[this]{auto panel=createStudyOverlay("목업에서 동작하는 범위",QSize(570,280));auto l=new QVBoxLayout(panel);auto info=label("명령 편집·추가·삭제·실행 취소·예제 파일 전환은 메모리에서 동작합니다.\n실제 오디오, 디스크 저장, 플러그인 실행, APO 설치는 하지 않습니다.",false,15);info->setWordWrap(true);l->addWidget(info);l->addWidget(button("닫기",[this]{closeStudyOverlay();}));});
    root->addWidget(menus);
    auto toolbar=new QWidget;auto bar=horizontal(toolbar);bar->setSpacing(t.soft?8:6);toolbar->setFixedHeight(48);
    bar->addWidget(iconButton("file-new","새로 만들기",[this]{documents["untitled.txt"]={{"주석","COMMENT",0,0,0,"새 목업 문서"}};loadStudyFile("untitled.txt");}));bar->addWidget(iconButton("folder-open","열기",[this]{showFileChoice();}));bar->addWidget(iconButton("save","저장",[this]{dirty=false;documentDirty[activeFile]=false;status->setText("저장됨 · 목업");}));
    bar->addSpacing(8);auto undo=iconButton("undo","실행 취소",[this]{undoStudy();});undo->setObjectName("studyUndoButton");undo->setEnabled(!undoByFile[activeFile].isEmpty());bar->addWidget(undo);auto redo=iconButton("redo","다시 실행",[this]{undoStudy(true);});redo->setObjectName("studyRedoButton");redo->setEnabled(!redoByFile[activeFile].isEmpty());bar->addWidget(redo);
    auto instant=new StudyToggle(&t,"즉시 적용");instant->setChecked(instantPreview);instant->setToolTip("실제 적용은 하지 않습니다. 목업의 표시 상태만 바뀝니다.");connect(instant,&QCheckBox::toggled,this,[this](bool b){instantPreview=b;status->setText(b?"즉시 적용 · 목업":"수동 적용 · 목업");});bar->addWidget(instant);
    status=label(dirty?"변경됨 · 목업":"저장됨 · 목업",true,12);bar->addWidget(status);bar->addStretch();bar->addWidget(label("장치",true,12));
    auto device=new StudyComboBox(&t);device->addItems({"CABLE Input · 예제 장치","Headphones · 예제 장치"});device->setMinimumWidth(230);device->setAccessibleName("장치 예제 선택");bar->addWidget(device);bar->addWidget(label("채널",true,12));auto channels=new StudyComboBox(&t);channels->addItems({"스테레오","7.1 (표시 예제)"});channels->setMinimumWidth(125);channels->setAccessibleName("장치 채널 구성 예제");bar->addWidget(channels);root->addWidget(toolbar);return chrome;
}
QWidget* Mockup::studyTabs() {
    auto tabs=new QTabBar;tabs->setExpanding(false);tabs->setDrawBase(false);tabs->setAccessibleName("설정 파일 탭");QStringList files=studyFiles();for(auto k:documents.keys())if(!files.contains(k))files<<k;
    for(const auto& file:files)tabs->addTab(file);tabs->setCurrentIndex(files.indexOf(activeFile));connect(tabs,&QTabBar::currentChanged,this,[this,files](int index){if(index>=0)QTimer::singleShot(0,this,[this,files,index]{loadStudyFile(files[index]);});});return tabs;
}
QWidget* Mockup::studyList() {
    scroll=new QScrollArea;scroll->setWidgetResizable(true);scroll->setFrameShape(QFrame::NoFrame);scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto contents=new QWidget;rowsLayout=new QVBoxLayout(contents);rowsLayout->setContentsMargins(t.soft?10:0,6,t.soft?18:8,8);rowsLayout->setSpacing(t.soft?8:4);
    for(int i=0;i<commands.size();++i) {
        bool opened=expandedStudyRows.contains(i);if((commands[i].hz>0 || commands[i].kind=="PRE") && i!=selected)opened=false;
        auto row=new CommandRow(&t,commands[i],i);row->selected=i==selected;row->detailOpen=opened;rows<<row;
        if(t.soft)row->setFixedHeight(62);
        row->toggle=[this,i]{commands[i].on=!commands[i].on;QTimer::singleShot(0,this,[this]{rebuild();});};
        connect(row,&QAbstractButton::clicked,this,[this,i]{QTimer::singleShot(0,this,[this,i]{selectRow(i);});});
        if(opened){QWidget* panel;if(t.soft){auto p=new SoftPanel(&t);p->selected=i==selected;p->off=!commands[i].on;panel=p;}else {auto p=new StudioStudyPanel(&t);p->active=i==selected;panel=p;}auto group=new QVBoxLayout(panel);group->setContentsMargins(0,0,0,0);group->setSpacing(0);group->addWidget(row);makeStudyBody(group,i);rowsLayout->addWidget(panel);if(i==selected)editor=panel;}
        else rowsLayout->addWidget(row);
    }
    auto add=button("+ 명령 추가",[this]{showPicker(commands.size());});add->setAccessibleName("목록 끝에 명령 추가");rowsLayout->addWidget(add,0,Qt::AlignLeft);rowsLayout->addStretch();
    scroll->setWidget(contents);QPalette cp=contents->palette();cp.setColor(QPalette::Window,t.soft?t.window:t.well);contents->setPalette(cp);contents->setAutoFillBackground(true);scroll->viewport()->setPalette(cp);scroll->viewport()->setAutoFillBackground(true);return scroll;
}
void Mockup::makeStudyBody(QVBoxLayout* group,int index) {
    const auto& c=commands[index];
    if(c.hz>0 || c.kind=="PRE") {
        if(t.soft)makeSoftEditor(group);else {auto old=rowsLayout;rowsLayout=group;makeEditor();rowsLayout=old;}
        return;
    }
    QWidget* body=nullptr;
    if(c.kind=="VST" || c.kind=="CONV" || c.kind=="INCLUDE")body=referenceBody(index);
    else if(c.kind=="COPY")body=routingBody(index);
    else if(c.kind=="GEQ")body=graphicBody(index);
    else body=scopeBody(index);
    group->addWidget(body);if(index==selected)editor=body;else return;
    auto actions=new QWidget;auto ac=horizontal(actions,t.soft?125:110);ac->setSpacing(8);
    auto enabledButton=button(c.on?"사용 중지":"사용",[this,index]{commands[index].on=!commands[index].on;QTimer::singleShot(0,this,[this]{rebuild();});});ac->addWidget(enabledButton);if(index==selected)enabled=enabledButton;
    ac->addWidget(button("+ 앞에 삽입",[this,index]{showPicker(index);}));ac->addWidget(button("원문 보기",[this,index]{auto content=createStudyOverlay("명령 원문 · 편집하지 않는 미리보기",QSize(720,230));auto l=new QVBoxLayout(content);auto source=new QPlainTextEdit(raw(commands[index]));source->setReadOnly(true);source->setFont(face(14,false,!t.soft));l->addWidget(source);l->addWidget(button("닫기",[this]{closeStudyOverlay();}));}));ac->addStretch();actions->setContentsMargins(0,0,0,8);group->addWidget(actions);
}
QWidget* Mockup::studyFooter(){auto w=new QWidget;w->setFixedHeight(25);auto l=horizontal(w);l->addWidget(label("48 kHz · 예제 문서 "+activeFile,true,12));l->addStretch();l->addWidget(label("오디오 엔진·플러그인 미연결 / 저장은 메모리만",true,12));return w;}

void Mockup::closeStudyOverlay(){if(studyOverlay){delete studyOverlay;studyOverlay=nullptr;}pickerSearch=nullptr;pickerList=nullptr;pickerAdd=nullptr;}
QWidget* Mockup::createStudyOverlay(QString title,QSize size) {
    closeStudyOverlay();studyOverlay=new StudyScrim(&t,this);studyOverlay->setGeometry(rect());studyOverlay->setObjectName("studyOverlay");
    QWidget* panel;if(t.soft)panel=new SoftPanel(&t);else panel=new StudioStudyPanel(&t);panel->setParent(studyOverlay);panel->setObjectName("studyOverlayPanel");size.setWidth(qMin(size.width(),width()-70));size.setHeight(qMin(size.height(),height()-100));panel->setGeometry((width()-size.width())/2,(height()-size.height())/2,size.width(),size.height());
    auto root=new QVBoxLayout(panel);root->setContentsMargins(24,18,24,18);root->setSpacing(16);auto top=new QWidget;auto l=horizontal(top);auto name=label(title,false,t.soft?20:17);name->setFont(face(t.soft?20:17,true));l->addWidget(name);l->addStretch();auto x=button("닫기",[this]{closeStudyOverlay();});l->addWidget(x);root->addWidget(top);
    auto content=new QWidget;root->addWidget(content,1);auto escape=new QShortcut(QKeySequence(Qt::Key_Escape),studyOverlay);escape->setContext(Qt::WidgetWithChildrenShortcut);connect(escape,&QShortcut::activated,this,[this]{closeStudyOverlay();});studyOverlay->show();studyOverlay->raise();return content;
}
void Mockup::showPicker(int index) {
    insertionIndex=qBound(0,index,int(commands.size()));auto page=createStudyOverlay(t.soft?"어떤 명령을 추가할까요?":"명령 추가",QSize(t.soft?650:890,620));auto root=new QVBoxLayout(page);root->setContentsMargins(0,0,0,0);root->setSpacing(12);
    auto where=label(insertionIndex<commands.size()?QString("%1번 %2 앞에 삽입합니다").arg(insertionIndex+1).arg(commands[insertionIndex].title):"목록 마지막에 추가합니다",true,13);root->addWidget(where);
    pickerSearch=new QLineEdit;pickerSearch->setPlaceholderText("이름 또는 명령어로 검색");pickerSearch->setMinimumHeight(42);pickerSearch->setAccessibleName("명령 검색");root->addWidget(pickerSearch);
    auto body=new QWidget;auto bl=horizontal(body);pickerList=new QListWidget;pickerList->setItemDelegate(new StudyPickerDelegate(&t,pickerList));pickerList->setAccessibleName("명령 검색 결과");bl->addWidget(pickerList,1);
    if(!t.soft){auto preview=new QLabel("항목을 고르면 원문과 설명을 확인할 수 있습니다.");preview->setMinimumWidth(270);preview->setWordWrap(true);preview->setAlignment(Qt::AlignLeft|Qt::AlignTop);preview->setContentsMargins(12,14,12,12);preview->setFont(face(14,false,true));bl->addWidget(preview,1);connect(pickerList,&QListWidget::currentRowChanged,this,[this,preview](int){if(auto item=pickerList->currentItem()){auto c=studyTemplates()[item->data(Qt::UserRole).toInt()];preview->setText("명령 미리보기\n\n"+c.title+"\n\n"+raw(c)+"\n\n"+softDescription(c));}});}
    root->addWidget(body,1);pickerAdd=button("선택한 명령 추가",[this]{insertPicked();});pickerAdd->setProperty("primary",true);root->addWidget(pickerAdd,0,Qt::AlignRight);
    connect(pickerSearch,&QLineEdit::textChanged,this,[this](QString s){filterPicker(s);});connect(pickerList,&QListWidget::itemDoubleClicked,this,[this](QListWidgetItem*){insertPicked();});filterPicker("");pickerSearch->setFocus();
}
void Mockup::filterPicker(QString query) {
    pickerList->clear();const auto catalog=studyTemplates();
    for(int i=0;i<catalog.size();++i){const auto& c=catalog[i];if(!(c.title+" "+c.kind+" "+raw(c)).contains(query,Qt::CaseInsensitive))continue;auto item=new QListWidgetItem(studyIcon(studyCommandIcon(c.kind),t.muted),c.title,pickerList);item->setData(Qt::UserRole,i);item->setData(Qt::UserRole+1,softDescription(c));}
    pickerAdd->setEnabled(pickerList->count()>0);if(pickerList->count())pickerList->setCurrentRow(0);else {auto empty=new QListWidgetItem("검색 결과가 없습니다",pickerList);empty->setFlags(Qt::NoItemFlags);}
}
void Mockup::insertPicked(){auto item=pickerList?pickerList->currentItem():nullptr;if(!item || !pickerAdd->isEnabled())return;Command c=studyTemplates()[item->data(Qt::UserRole).toInt()];int at=insertionIndex;closeStudyOverlay();insertStudyCommand(c,at);}
void Mockup::insertStudyCommand(Command c,int at){commands.insert(qBound(0,at,int(commands.size())),c);selected=qBound(0,at,int(commands.size()-1));expandedStudyRows={selected};expanded=true;rebuild();}
void Mockup::deleteStudyCommand(){if(commands.isEmpty())return;commands.removeAt(selected);if(commands.isEmpty())commands={{"주석","COMMENT",0,0,0,"빈 설정"}};selected=qMin(selected,int(commands.size()-1));expandedStudyRows={selected};rebuild();}

void Mockup::showFileChoice(int index) {
    auto content=createStudyOverlay(index<0?"예제 설정 열기":"파일 다시 지정 · 예제에서 선택",QSize(660,380));auto l=new QVBoxLayout(content);l->setContentsMargins(0,0,0,0);l->addWidget(label("실제 파일에 접근하지 않는 목업용 목록입니다.",true,13));
    if(index<0){for(QString file:studyFiles())l->addWidget(button(file,[this,file]{closeStudyOverlay();QTimer::singleShot(0,this,[this,file]{loadStudyFile(file);});}));}
    else {QString name=commands[index].kind=="INCLUDE"?"Presets\\headphones-fixed.txt":(commands[index].kind=="VST"?"Plugins\\Room Processor.vst3":"IRs\\nearfield.wav");l->addWidget(button(name,[this,index,name]{commands[index].detail=name;commands[index].missing=false;closeStudyOverlay();QTimer::singleShot(0,this,[this]{rebuild();});}));}
    l->addStretch();
}
