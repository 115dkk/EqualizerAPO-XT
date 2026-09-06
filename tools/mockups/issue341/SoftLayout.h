// Deliberately separate from the Studio layout: Soft is not its color variant.
#pragma once

void Mockup::rebuildSoft() {
    mainLayout=new QVBoxLayout(this);const int side=qMax(28,(width()-1240)/2);mainLayout->setContentsMargins(side,12,side,8);mainLayout->setSpacing(10);
    auto top=new QWidget;auto topL=horizontal(top);top->setFixedHeight(40);
    auto brand=label("Equalizer APO XT",false,17);brand->setFont(softFont(17,true));topL->addWidget(brand);topL->addSpacing(4);topL->addWidget(label("Soft",true,14));topL->addStretch();
    topL->addWidget(label("디자인 목업 · 오디오 엔진 미연결",true,12));
    auto theme=new QComboBox;theme->setMinimumHeight(38);theme->addItems({"Studio · 다크","Studio · 라이트","Soft · 다크","Soft · 라이트"});theme->setAccessibleName("목업 테마");
    QStringList names={"studio-dark","studio-light","soft-dark","soft-light"};theme->setCurrentIndex(names.indexOf(themeName));topL->addWidget(theme);
    connect(theme,&QComboBox::currentIndexChanged,this,[this,names](int i){QTimer::singleShot(0,this,[this,names,i]{themeName=names[i];t=themeFor(themeName);rebuild();});});mainLayout->addWidget(top);

    auto toolbar=new QWidget;toolbar->setFixedHeight(52);auto tl=horizontal(toolbar);
    auto nameBlock=new QWidget;auto nl=new QVBoxLayout(nameBlock);nl->setContentsMargins(0,0,0,0);nl->setSpacing(3);
    auto filename=label("config.txt",false,25);filename->setFont(softFont(25,true));nl->addWidget(filename);status=label(dirty?"목업 값 변경됨":"12개 명령 · 위에서 아래로 적용됩니다",true,13);nl->addWidget(status);tl->addWidget(nameBlock);tl->addStretch();
    auto device=new QWidget;auto dl=new QVBoxLayout(device);dl->setContentsMargins(0,0,16,0);dl->setSpacing(4);dl->addWidget(label("출력 장치 · 스테레오",true,12));dl->addWidget(label("CABLE Input · VB-Audio Virtual Cable",false,14));tl->addWidget(device);
    tl->addWidget(button("초기 상태로",[this]{commands=fixture();selected=3;dirty=false;expanded=true;code=false;rebuild();}));mainLayout->addWidget(toolbar);

    scroll=new QScrollArea;scroll->setWidgetResizable(true);scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);scroll->setFrameShape(QFrame::NoFrame);
    auto contents=new QWidget;rowsLayout=new QVBoxLayout(contents);rowsLayout->setContentsMargins(0,0,12,3);rowsLayout->setSpacing(8);
    for(int i=0;i<commands.size();++i) {
        auto row=new CommandRow(&t,commands[i],i);row->selected=i==selected;row->detailOpen=i==selected && expanded;rows.append(row);
        row->toggle=[this,i]{const int pos=scroll->verticalScrollBar()->value();commands[i].on=!commands[i].on;dirty=true;QTimer::singleShot(0,this,[this,pos]{rebuild();qApp->processEvents();scroll->verticalScrollBar()->setValue(pos);});};
        connect(row,&QAbstractButton::clicked,this,[this,i]{QTimer::singleShot(0,this,[this,i]{selectRow(i);});});
        if(row->detailOpen) {
            auto panel=new SoftPanel(&t);panel->selected=true;panel->off=!commands[i].on;auto group=new QVBoxLayout(panel);group->setContentsMargins(0,0,0,0);group->setSpacing(0);group->addWidget(row);
            makeSoftEditor(group);rowsLayout->addWidget(panel);
        } else rowsLayout->addWidget(row);
    }
    rowsLayout->addStretch(1);scroll->setWidget(contents);
    QPalette cp=contents->palette();cp.setColor(QPalette::Window,t.window);contents->setPalette(cp);contents->setAutoFillBackground(true);
    scroll->viewport()->setPalette(cp);scroll->viewport()->setAutoFillBackground(true);mainLayout->addWidget(scroll,1);

    auto analysis=new SoftPanel(&t);softAnalysis=analysis;analysis->setFixedHeight(height()<960?188:208);auto al=new QVBoxLayout(analysis);al->setContentsMargins(24,10,24,14);al->setSpacing(0);
    auto graphHead=new QWidget;graphHead->setFixedHeight(40);auto gl=horizontal(graphHead);
    auto graphTitle=label("합산 응답",false,17);graphTitle->setFont(softFont(17,true));gl->addWidget(graphTitle);gl->addWidget(label("크기 (dB) · L",true,13));gl->addStretch();
    traceNote=label(dirty?"값 변경됨 · 응답 재계산 안 됨":"응답 예시 · 원본 캡처 기준",true,12);gl->addWidget(traceNote);
    auto warning=label("△ 예시 피크 +0.8 dB",false,13);warning->setProperty("warning",true);gl->addSpacing(14);gl->addWidget(warning);al->addWidget(graphHead);
    response=new Response(&t);response->stale=dirty;al->addWidget(response,1);mainLayout->addWidget(analysis);
    auto footer=new QWidget;footer->setFixedHeight(20);auto fl=horizontal(footer);fl->addWidget(label("48 kHz · 설정 파일은 저장하지 않습니다",true,12));fl->addStretch();fl->addWidget(label("Soft / 형태 재설계 02",true,12));mainLayout->addWidget(footer);
    update();
}

void Mockup::makeSoftEditor(QVBoxLayout* group) {
    editor=new QWidget;auto el=new QVBoxLayout(editor);el->setContentsMargins(125,0,28,18);el->setSpacing(12);
    const Command c=commands[selected];
    if(c.hz>0 || c.kind=="PRE") {
        auto params=new QWidget;params->setMaximumWidth(820);auto pl=horizontal(params);pl->setSpacing(40);
        auto param=[&](QString caption,QString unit,double value,double min,double max,int decimals,QString role) {
            auto block=new QWidget;auto vl=new QVBoxLayout(block);vl->setContentsMargins(0,0,0,0);vl->setSpacing(4);
            auto title=label(caption,true,14);title->setAlignment(Qt::AlignCenter);vl->addWidget(title);
            const bool log=role!="gain";
            const auto toDial=[=](double v){return qRound((log?std::log(v/min)/std::log(max/min):(v-min)/(max-min))*1000);};
            const auto fromDial=[=](int n){return log?min*std::pow(max/min,n/1000.):min+(max-min)*n/1000.;};
            auto knob=new Knob(&t);knob->bipolar=role=="gain";knob->setRange(0,1000);knob->setValue(toDial(value));knob->setAccessibleName(caption+" 노브");knob->setEnabled(c.on);knobs.append(knob);vl->addWidget(knob,0,Qt::AlignHCenter);
            auto spin=new QDoubleSpinBox;spin->setRange(min,max);spin->setDecimals(decimals);spin->setValue(value);spin->setSuffix(unit.isEmpty()?"":" "+unit);spin->setFont(softFont(23,true));spin->setAlignment(Qt::AlignCenter);spin->setFixedSize(180,44);spin->setButtonSymbols(QAbstractSpinBox::NoButtons);spin->setSingleStep(role=="hz"?1:.1);spin->setAccessibleName(caption);spin->setProperty("role",role);spin->setEnabled(c.on);if(role=="gain" && value>0)spin->setPrefix("+");values.append(spin);vl->addWidget(spin,0,Qt::AlignHCenter);pl->addWidget(block,1);
            connect(spin,qOverload<double>(&QDoubleSpinBox::valueChanged),this,[=](double n){
                if(role=="hz")commands[selected].hz=n;else if(role=="gain")commands[selected].gain=n;else commands[selected].q=n;
                if(role=="gain")spin->setPrefix(n>0?"+":"");QSignalBlocker lock(knob);knob->setValue(toDial(n));changed();
            });
            connect(knob,&QDial::valueChanged,this,[=](int n){spin->setValue(fromDial(n));});
        };
        if(c.hz>0)param("주파수","Hz",c.hz,20,20000,0,"hz");
        if(c.kind!="HP" && c.kind!="AP")param("게인","dB",c.gain,-24,24,1,"gain");
        if(c.q>0)param("Q 값","",c.q,.1,10,2,"q");
        if(c.kind=="PRE")pl->addStretch(2);
        el->addWidget(params);
    } else {
        QString detail=c.detail;
        if(c.kind=="INCLUDE")detail="example.txt\n내부 명령 2개 · 프리앰프 −2 dB / 피킹 3,000 Hz, −3 dB, Q 1";
        if(c.kind=="CH")detail="L · R 선택이 아래 명령에 적용됩니다. 다음 Channel 명령에서 범위가 바뀝니다.";
        auto body=label(detail,false,16);body->setWordWrap(true);body->setMinimumHeight(54);el->addWidget(body);
    }
    auto actions=new QWidget;auto ac=horizontal(actions);ac->setSpacing(8);
    enabled=button(c.on?"사용 중":"사용 안 함",[this]{int pos=scroll->verticalScrollBar()->value();commands[selected].on=!commands[selected].on;dirty=true;QTimer::singleShot(0,this,[this,pos]{rebuild();qApp->processEvents();scroll->verticalScrollBar()->setValue(pos);});});enabled->setCheckable(true);enabled->setChecked(c.on);enabled->setAccessibleName("선택 명령 사용 여부");ac->addWidget(enabled);
    if(c.kind=="COMMENT"){enabled->setText("주석");enabled->setChecked(false);enabled->setEnabled(false);}
    ac->addWidget(button(code?"원문 닫기":"원문 보기",[this]{code=!code;QTimer::singleShot(0,this,[this]{rebuild();});}));
    ac->addWidget(button("+ 앞에 삽입"));ac->addWidget(button("삭제"));ac->addStretch();ac->addWidget(label(c.on?"적용 채널 L · R":"꺼진 명령 · 값은 유지됩니다",true,13));el->addWidget(actions);
    if(code){auto source=label(raw(c),false,14);source->setWordWrap(true);source->setTextInteractionFlags(Qt::TextSelectableByMouse);el->addWidget(source);}
    group->addWidget(editor);
}
