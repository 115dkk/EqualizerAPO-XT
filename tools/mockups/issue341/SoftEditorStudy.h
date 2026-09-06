#pragma once

void Mockup::buildSoftStudy() {
    mainLayout=new QVBoxLayout(this);mainLayout->setContentsMargins(28,8,28,8);mainLayout->setSpacing(10);
    mainLayout->addWidget(studyChrome());mainLayout->addWidget(studyTabs());
    auto list=studyList();mainLayout->addWidget(list,1);
    if(analysisVisible)mainLayout->addWidget(studyAnalysis());else {response=nullptr;traceNote=nullptr;}
    mainLayout->addWidget(studyFooter());update();
}
