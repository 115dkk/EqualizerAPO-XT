#pragma once

void Mockup::buildStudioStudy() {
    mainLayout=new QVBoxLayout(this);mainLayout->setContentsMargins(16,4,16,6);mainLayout->setSpacing(7);
    mainLayout->addWidget(studyChrome());
    auto documentStrip=new StudioStudyPanel(&t);auto strip=new QHBoxLayout(documentStrip);strip->setContentsMargins(8,0,8,0);strip->addWidget(studyTabs(),1);strip->addWidget(label("CONFIGURATION / "+activeFile.toUpper(),true,12));mainLayout->addWidget(documentStrip);
    auto listFrame=new StudioStudyPanel(&t);auto layout=new QVBoxLayout(listFrame);layout->setContentsMargins(9,0,6,5);layout->setSpacing(0);
    auto caption=new QWidget;auto top=horizontal(caption,12);caption->setFixedHeight(32);top->addWidget(label("처리 체인",false,13));top->addWidget(label(QString::number(commands.size())+" 줄",true,12));top->addStretch();top->addWidget(label("위에서 아래로 처리 · 채널 범위는 명령 순서에 따름",true,12));layout->addWidget(caption);layout->addWidget(studyList(),1);mainLayout->addWidget(listFrame,1);
    if(analysisVisible)mainLayout->addWidget(studyAnalysis());else {response=nullptr;traceNote=nullptr;}
    mainLayout->addWidget(studyFooter());update();
}
