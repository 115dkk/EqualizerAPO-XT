#pragma once

QWidget* Mockup::referenceBody(int index) {
    const Command c=commands[index];auto body=new QWidget;auto layout=new QVBoxLayout(body);layout->setContentsMargins(t.soft?125:110,4,24,12);layout->setSpacing(t.soft?12:9);
    auto identity=new QWidget;auto id=horizontal(identity);id->setSpacing(12);
    auto glyph=new QLabel;glyph->setPixmap(studyIcon(studyCommandIcon(c.kind),c.missing?t.warning:t.muted).pixmap(t.soft?38:28,t.soft?38:28));id->addWidget(glyph);
    auto info=new QWidget;auto il=new QVBoxLayout(info);il->setContentsMargins(0,0,0,0);il->setSpacing(4);
    QString file=c.detail.section('\\',-1);QString folder=c.detail.left(c.detail.lastIndexOf('\\')+1);
    auto filename=new StudyElidedLabel(&t,file);filename->setFont(face(t.soft?18:16,true));filename->setTextInteractionFlags(Qt::TextSelectableByMouse);il->addWidget(filename);
    auto location=label(folder,true,13);location->setFont(face(13,false,!t.soft));il->addWidget(location);id->addWidget(info,0);id->addSpacing(t.soft?12:8);
    auto browse=button(c.missing?"파일 다시 지정…":"파일 선택…",[this,index]{showFileChoice(index);});browse->setProperty("primary",c.missing);id->addWidget(browse,0,Qt::AlignVCenter);
    if(c.kind=="VST")id->addWidget(button("패널 열기",[this]{auto page=createStudyOverlay("플러그인 고유 창",QSize(620,270));auto l=new QVBoxLayout(page);auto info=label("이 영역은 외부 VST가 그리는 창입니다.\n편집기의 Soft / Studio 스킨이 플러그인 UI까지 바꾸지는 않습니다.\n이 목업에서는 라이브러리를 로드하지 않습니다.",false,15);info->setWordWrap(true);l->addWidget(info);l->addWidget(button("닫기",[this]{closeStudyOverlay();}));}));id->addStretch();layout->addWidget(identity);
    if(c.missing){auto warning=label(t.soft?"파일을 찾을 수 없습니다. 사용할 파일을 다시 지정해 주세요. (목업 상태)":"MISSING  ·  참조 파일을 찾을 수 없음 / 목업 상태",false,13);warning->setProperty("warning",true);layout->addWidget(warning);}
    else if(c.kind=="CONV")layout->addWidget(label("IR 예제 메타데이터   48 kHz  ·  2채널  ·  4,096 samples  ·  85.3 ms",true,13));
    else if(c.kind=="INCLUDE"){auto open=button("설정 내용 열기",[this,index]{QString file=commands[index].detail.section('\\',-1);if(!documents.contains(file))documents[file]={{"프리앰프","PRE",0,-2,0,{}},{"피킹 필터","PK",3000,-3,1,{}}};closeStudyOverlay();loadStudyFile(file);});layout->addWidget(open,0,Qt::AlignLeft);}
    if(c.kind=="VST") {
        auto buses=new QWidget;auto bl=horizontal(buses);bl->setSpacing(t.soft?16:10);
        bl->addWidget(label("VST3",true,12));bl->addWidget(label("입력",true,13));auto in=new StudyComboBox(&t);in->addItems({"Auto","Mono","Stereo","5.1","7.1"});in->setCurrentText(c.inputBus);in->setMinimumWidth(144);in->setAccessibleName("VST 입력 버스");bl->addWidget(in);
        bl->addWidget(label("→",true,18));bl->addWidget(label("출력",true,13));auto out=new StudyComboBox(&t);out->addItems({"Auto","Mono","Stereo","5.1","7.1"});out->setCurrentText(c.outputBus);out->setMinimumWidth(144);out->setAccessibleName("VST 출력 버스");bl->addWidget(out);bl->addStretch();layout->addWidget(buses);
        auto summary=label("요청 레이아웃 예제 · 실제 플러그인 협상은 실행하지 않습니다",true,12);layout->addWidget(summary);
        connect(in,&QComboBox::currentTextChanged,this,[this,index,summary](QString value){commands[index].inputBus=value;recordStudyChange();summary->setText("요청 변경됨 · 실제 협상은 실행하지 않습니다");});
        connect(out,&QComboBox::currentTextChanged,this,[this,index,summary](QString value){commands[index].outputBus=value;recordStudyChange();summary->setText("요청 변경됨 · 실제 협상은 실행하지 않습니다");});
    }
    return body;
}
QWidget* Mockup::routingBody(int index) {
    auto body=new QWidget;auto layout=new QVBoxLayout(body);layout->setContentsMargins(t.soft?125:110,6,24,12);layout->setSpacing(10);
    auto view=new RoutingStudyView(&t);view->left=commands[index].gain;view->right=commands[index].q;layout->addWidget(view);
    auto edit=new QWidget;auto el=horizontal(edit);el->setSpacing(10);
    el->addWidget(label("L 출력에 섞을 비율",true,13));
    for(int source=0;source<2;++source){el->addWidget(label(source?"입력 R":"입력 L",false,13));auto value=new QDoubleSpinBox;value->setButtonSymbols(QAbstractSpinBox::NoButtons);value->setRange(-2,2);value->setDecimals(2);value->setSingleStep(.05);value->setValue(source?view->right:view->left);value->setFixedSize(110,42);value->setFont(face(17,false,!t.soft));value->setAccessibleName(source?"R 입력 계수":"L 입력 계수");el->addWidget(value);
        connect(value,qOverload<double>(&QDoubleSpinBox::valueChanged),this,[=](double n){if(source){commands[index].q=n;view->right=n;}else {commands[index].gain=n;view->left=n;}commands[index].detail="L = "+number(view->left,2)+" × L + "+number(view->right,2)+" × R";rows[index]->c=commands[index];rows[index]->update();recordStudyChange();view->update();if(response){response->stale=true;response->update();}status->setText("변경됨 · 목업");});}
    el->addStretch();layout->addWidget(edit);layout->addWidget(label("계수는 dB가 아닌 선형 비율입니다. 이 예제에서 R 출력은 그대로 통과합니다.",true,12));return body;
}
QWidget* Mockup::graphicBody(int index) {
    if(commands[index].points.isEmpty())commands[index].points={{20,0},{100,3},{1000,-2},{10000,4}};
    auto body=new QWidget;auto layout=new QVBoxLayout(body);layout->setContentsMargins(t.soft?70:110,6,24,12);layout->setSpacing(10);
    auto options=new QWidget;auto ol=horizontal(options);ol->addWidget(label("대역",true,13));auto bands=new StudyComboBox(&t);bands->addItems({"가변 대역","15 대역","31 대역"});bands->setCurrentIndex(commands[index].points.size()==15?1:commands[index].points.size()==31?2:0);bands->setAccessibleName("그래픽 EQ 대역 수");bands->setMinimumWidth(142);ol->addWidget(bands);ol->addStretch();ol->addWidget(label("지점을 선택하고 숫자 또는 방향키로 조절",true,12));layout->addWidget(options);
    graphicPlot=new GraphicStudyPlot(&t,commands[index].points);auto plot=graphicPlot;layout->addWidget(plot);
    auto controls=new QWidget;auto cl=horizontal(controls);cl->addWidget(label("선택 지점",true,13));auto selectedPoint=label("100 Hz",false,15);cl->addWidget(selectedPoint);cl->addWidget(label("게인",true,13));auto gain=new QDoubleSpinBox;gain->setButtonSymbols(QAbstractSpinBox::NoButtons);gain->setRange(-12,12);gain->setDecimals(1);gain->setSuffix(" dB");gain->setValue(plot->points[1].y());gain->setSingleStep(.1);gain->setFixedSize(140,42);gain->setAccessibleName("선택 지점 게인");cl->addWidget(gain);cl->addStretch();cl->addWidget(label("연결선은 편집 지점의 표시이며 실제 FIR 응답이 아닙니다",true,12));layout->addWidget(controls);
    plot->selectionChanged=[=](int p){selectedPoint->setText(number(plot->points[p].x(),0)+" Hz");QSignalBlocker lock(gain);gain->setValue(plot->points[p].y());};
    auto changedPoints=[this,index,plot,gain](QVector<QPointF> points){commands[index].points=points;commands[index].detail=QString("가변 대역 · %1개 지점").arg(points.size());recordStudyChange();QSignalBlocker lock(gain);gain->setValue(points[plot->pointIndex].y());status->setText("변경됨 · 목업");};
    plot->pointsChanged=changedPoints;
    connect(gain,qOverload<double>(&QDoubleSpinBox::valueChanged),this,[=](double n){plot->points[plot->pointIndex].setY(n);changedPoints(plot->points);plot->update();});
    connect(bands,&QComboBox::currentIndexChanged,this,[=](int mode){int count=mode==1?15:mode==2?31:4;QVector<QPointF> values;if(mode==0)values={{20,0},{100,3},{1000,-2},{10000,4}};else for(int i=0;i<count;++i)values.append(QPointF(20*std::pow(1000.,i/double(count-1)),0));plot->points=values;plot->pointIndex=0;plot->selectionChanged(0);changedPoints(values);plot->update();});
    return body;
}
QWidget* Mockup::scopeBody(int index) {
    auto body=new QWidget;auto layout=new QVBoxLayout(body);layout->setContentsMargins(t.soft?125:110,4,24,12);auto line=new QWidget;auto l=horizontal(line);const auto c=commands[index];
    if(c.kind=="CH"){
        l->addWidget(label("다음 명령부터 적용할 채널",true,13));for(QString channel:{"L","R"}){auto toggle=button(channel);toggle->setEnabled(true);toggle->setCheckable(true);toggle->setChecked(c.detail.contains(channel));toggle->setMinimumWidth(55);l->addWidget(toggle);connect(toggle,&QPushButton::toggled,this,[this,index,channel](bool checked){QStringList channels=commands[index].detail.split(" · ",Qt::SkipEmptyParts);if(checked && !channels.contains(channel))channels<<channel;if(!checked)channels.removeAll(channel);commands[index].detail=channels.join(" · ");recordStudyChange();});}
    }else if(c.kind=="DEV" || c.kind=="STAGE"){
        l->addWidget(label(c.kind=="DEV"?"이후 명령을 적용할 장치":"처리 단계",true,13));auto choice=new StudyComboBox(&t);if(c.kind=="DEV")choice->addItems({"CABLE Input","Headphones · 예제"});else choice->addItems({"pre-mix","post-mix"});choice->setCurrentText(c.detail);choice->setMinimumWidth(250);l->addWidget(choice);connect(choice,&QComboBox::currentTextChanged,this,[this,index](QString value){commands[index].detail=value;recordStudyChange();});
    }else if(c.kind=="DELAY"){
        l->addWidget(label("지연",true,13));auto delay=new QDoubleSpinBox;delay->setButtonSymbols(QAbstractSpinBox::NoButtons);delay->setRange(0,1000);delay->setValue(c.detail.section(' ',0,0).toDouble());delay->setSuffix(" ms");delay->setMinimumSize(150,42);l->addWidget(delay);connect(delay,qOverload<double>(&QDoubleSpinBox::valueChanged),this,[this,index](double n){commands[index].detail=number(n,1)+" ms";recordStudyChange();});
    }else {auto description=label(c.detail,false,15);description->setWordWrap(true);l->addWidget(description);}
    l->addStretch();layout->addWidget(line);return body;
}
QWidget* Mockup::studyAnalysis() {
    QWidget* panel=t.soft?static_cast<QWidget*>(new SoftPanel(&t)):static_cast<QWidget*>(new StudioStudyPanel(&t));panel->setMinimumHeight(t.soft?194:220);panel->setMaximumHeight(t.soft?224:272);
    auto root=new QVBoxLayout(panel);root->setContentsMargins(t.soft?22:14,8,t.soft?22:14,12);root->setSpacing(4);
    auto head=new QWidget;auto h=horizontal(head);h->addWidget(label("합산 응답",false,16));h->addSpacing(8);
    for(QString mode:{"크기","위상","군지연"}){auto b=button(mode,[this,mode]{analysisMetric=mode;rebuild();});b->setCheckable(true);b->setChecked(analysisMetric==mode);h->addWidget(b);}
    h->addStretch();traceNote=label(analysisMetric=="크기"?"배치 검토용 응답 예시 · 현재 파일 계산값 아님":"엔진 미연결 · 계산 결과 없음",true,12);h->addWidget(traceNote);h->addWidget(button("분석 설정…",[this]{showAnalysisSettings();}));root->addWidget(head);
    auto plotRow=new QWidget;auto pl=horizontal(plotRow);pl->setSpacing(14);
    if(!t.soft){auto controls=new QWidget;controls->setFixedWidth(226);auto g=new QGridLayout(controls);g->setContentsMargins(0,0,0,0);g->setHorizontalSpacing(10);g->setVerticalSpacing(4);
        const QStringList labels={"소스","채널","해상도"};for(int row=0;row<3;++row){g->addWidget(label(labels[row],true,12),row,0);auto combo=new StudyComboBox(&t);if(row==0)combo->addItems({"현재 파일"});else if(row==1){combo->addItems({"L","R"});combo->setCurrentText(analysisChannel);}else {combo->addItems({"65536","262144","1048576"});combo->setCurrentText(analysisResolution);}combo->setMinimumWidth(130);g->addWidget(combo,row,1);if(row==1)connect(combo,&QComboBox::currentTextChanged,this,[this](QString v){analysisChannel=v;});if(row==2)connect(combo,&QComboBox::currentTextChanged,this,[this](QString v){analysisResolution=v;});}
        pl->addWidget(controls);
    }
    response=new Response(&t);response->stale=dirty;response->studyUnits=true;
    if(analysisMetric!="크기")response->unavailable=analysisMetric+" 응답은 실제 엔진 연결 후 표시됩니다";
    pl->addWidget(response,1);root->addWidget(plotRow,1);
    auto facts=new QWidget;auto f=horizontal(facts);f->addWidget(label(analysisMetric=="크기"?"예시 피크 +0.8 dB":"피크 —",true,12));f->addSpacing(15);f->addWidget(label("지연 —   초기화 —   CPU —",true,12));f->addStretch();f->addWidget(label(analysisChannel+" · 48 kHz · 실제 측정 아님",true,12));root->addWidget(facts);return panel;
}
void Mockup::showAnalysisSettings() {
    auto page=createStudyOverlay("분석 설정",QSize(t.soft?580:640,460));auto root=new QVBoxLayout(page);auto form=new QFormLayout;form->setVerticalSpacing(12);
    auto source=new StudyComboBox(&t);source->addItem("현재 파일 · "+activeFile);source->setMinimumHeight(36);form->addRow("소스",source);
    auto channel=new StudyComboBox(&t);channel->addItems({"L","R"});channel->setCurrentText(analysisChannel);form->addRow("채널",channel);
    auto resolution=new StudyComboBox(&t);resolution->addItems({"65536","262144","1048576"});resolution->setCurrentText(analysisResolution);form->addRow("해상도",resolution);
    auto delay=new StudyToggle(&t,"기본 지연 포함");delay->setChecked(includeDelay);form->addRow("위상·군지연",delay);root->addLayout(form);auto note=label("배치는 하단 고정입니다. 상단·우측 도킹과 실제 응답 계산은 이번 목업에 포함하지 않았습니다.",true,12);note->setWordWrap(true);root->addWidget(note);
    auto apply=button("표시 설정 적용",[this,channel,resolution,delay]{analysisChannel=channel->currentText();analysisResolution=resolution->currentText();includeDelay=delay->isChecked();closeStudyOverlay();QTimer::singleShot(0,this,[this]{rebuild();});});root->addWidget(apply,0,Qt::AlignRight);
}
