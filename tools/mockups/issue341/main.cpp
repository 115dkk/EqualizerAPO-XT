// Standalone design prototype for #341. No APO, device, registry or config writes.
#include <QtWidgets>
#include <QtTest/QTest>
#include <QSvgRenderer>
#include <cmath>
#include <functional>

struct Theme {
    QString name;
    QColor window, surface, well, ink, muted, line, accent, selected, warning;
    bool soft = false;
    bool light = false;
    int radius() const { return soft ? 12 : 7; }
};

static Theme themeFor(QString name) {
    if (name == "soft-dark") return {"Soft", QColor("#23221f"), QColor("#302f2a"), QColor("#282722"), QColor("#eeeae1"), QColor("#b7b3a8"), QColor("#4a4840"), QColor("#a2b9d8"), QColor("#3e4751"), QColor("#e1bd86"), true, false};
    if (name == "studio-light") return {"Studio", QColor("#eef1f5"), QColor("#f9fafc"), QColor("#f1f3f7"), QColor("#202b3d"), QColor("#5c6675"), QColor("#d3d9e2"), QColor("#285fb5"), QColor("#e9f0fb"), QColor("#865218"), false, true};
    if (name == "soft-light") return {"Soft", QColor("#f2f0e9"), QColor("#fcfbf7"), QColor("#f0eee7"), QColor("#343730"), QColor("#73746b"), QColor("#dfded3"), QColor("#4d719f"), QColor("#e4ebf3"), QColor("#886032"), true, true};
    return {"Studio", QColor("#14181f"), QColor("#1c222b"), QColor("#181d25"), QColor("#e6ebf3"), QColor("#a5afbf"), QColor("#343e4c"), QColor("#8db7ff"), QColor("#243448"), QColor("#ebba79"), false, false};
}

static QFont face(int px = 15, bool strong = false, bool numeric = false) {
    QFont f;
    f.setFamilies(numeric ? QStringList{"EAPO Mono", "EAPO Sans KR", "Malgun Gothic"} : QStringList{"EAPO Sans KR", "EAPO Sans", "Malgun Gothic"});
    f.setPixelSize(px);
    f.setWeight(strong ? QFont::DemiBold : QFont::Medium);
    return f;
}
static void text(QPainter& p, QRectF r, const QString& s, QColor c, int px=15, bool strong=false, int align=Qt::AlignLeft|Qt::AlignVCenter, bool mono=false) {
    p.setFont(face(px,strong,mono)); p.setPen(c); p.drawText(r,align,s);
}
static QString number(double n, int decimals=1, bool sign=false) {
    return (sign && n > 0 ? "+" : "") + QString::number(n,'f',decimals);
}

struct Command {
    QString title, kind;
    double hz=0, gain=0, q=0;
    QString detail;
    bool on=true;
    bool missing=false;
    QString inputBus="Stereo",outputBus="Stereo";
    QVector<QPointF> points;
};
static QVector<Command> fixture() {
    return {
        {"채널", "CH",0,0,0,"L · R"},
        {"프리앰프", "PRE",0,-2,0,{}},
        {"하이패스 필터", "HP",30,0,.71,{}},
        {"로우셸프 필터", "LS",100,3,.71,{}},
        {"피킹 필터", "PK",250,-4,2,{}},
        {"피킹 필터", "PK",1000,6,.71,{}},
        {"올패스 필터", "AP",800,0,.71,{}},
        {"하이셸프 필터", "HS",8000,-2.5,.71,{}},
        {"채널 복사", "COPY",0,0,0,"L = 0.5 × L + 0.5 × R"},
        {"지연", "DELAY",0,0,0,"10 ms"},
        {"파일 포함", "INCLUDE",0,0,0,"example.txt"},
        {"그래픽 EQ", "GEQ",0,0,0,"20: 0   100: +3   1k: −2   10k: +4 dB"},
        {"주석", "COMMENT",0,0,0,"Room correction, measured 2026-08"}
    };
}
static QString raw(const Command& c) {
    if(c.kind=="VST")return "VSTPlugin: Library \""+c.detail+"\" Input "+c.inputBus+" Output "+c.outputBus;
    if(c.kind=="CONV")return "Convolution: "+c.detail;
    if(c.kind=="DEV")return "Device: "+c.detail;
    if(c.kind=="STAGE")return "Stage: "+c.detail;
    if(c.kind == "CH") return "Channel: "+QString(c.detail).replace(" · "," ");
    if(c.kind == "PRE") return "Preamp: " + number(c.gain) + " dB";
    if(c.hz > 0) return "Filter: " + QString(c.on ? "ON " : "OFF ") + c.kind + " Fc " + number(c.hz,0) + " Hz" + ((c.kind=="HP" || c.kind=="AP") ? "" : " Gain " + number(c.gain) + " dB") + " Q " + number(c.q,2);
    if(c.kind=="COPY") return "Copy: "+QString(c.detail).replace(" × ","*").remove(' ');
    if(c.kind=="DELAY") return "Delay: "+c.detail;
    if(c.kind=="INCLUDE") return "Include: "+c.detail;
    if(c.kind=="GEQ") {if(c.points.isEmpty())return "GraphicEQ: 20 0; 100 3; 1000 -2; 10000 4";QStringList pts;for(auto p:c.points)pts<<number(p.x(),0)+" "+number(p.y(),1);return "GraphicEQ: "+pts.join("; ");}
    return "# " + c.detail;
}

#include "SoftPresentation.h"

class Surface : public QWidget {
public:
    Theme* t;
    explicit Surface(Theme* theme, QWidget* parent=nullptr):QWidget(parent),t(theme){}
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);p.setBrush(t->surface);p.drawRoundedRect(rect(),t->radius(),t->radius());
        if (!t->soft) {p.setPen(t->line); p.drawLine(8,0,width()-8,0);}
    }
};

class Knob : public QDial {
public:
    Theme* t;
    bool bipolar=false;
    explicit Knob(Theme* theme, QWidget* parent=nullptr):QDial(parent),t(theme) {
        setFixedSize(t->soft?104:72,t->soft?104:72); setFocusPolicy(Qt::StrongFocus);setCursor(Qt::OpenHandCursor);
    }
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);p.setRenderHints(QPainter::Antialiasing|QPainter::TextAntialiasing);
        if(t->soft){paintSoftKnob(p,*t,size(),(value()-minimum())/double(qMax(1,maximum()-minimum())),bipolar,isEnabled(),hasFocus());return;}
        QRectF track(9,9,54,54);
        if(t->soft) {
            p.setBrush(t->window);p.setPen(Qt::NoPen);p.drawEllipse(QRectF(13,15,46,46));
            p.setBrush(t->surface.lighter(t->light?101:115));p.drawEllipse(QRectF(13,12,46,46));
        }
        p.setPen(QPen(t->line,t->soft?4:2,Qt::SolidLine,Qt::RoundCap)); p.setBrush(Qt::NoBrush); p.drawArc(track,225*16,-270*16);
        double v=(value()-minimum())/double(qMax(1,maximum()-minimum()));
        p.setPen(QPen(isEnabled()?t->accent:t->muted,t->soft?4:2.5,Qt::SolidLine,Qt::RoundCap));
        if(bipolar) {
            p.drawArc(track,90*16,int(-270*16*(v-.5)));
            p.setPen(QPen(t->muted,1));p.drawLine(QPointF(36,3),QPointF(36,8));
        } else p.drawArc(track,225*16,int(-270*16*v));
        double a=(225-270*v)*3.141592653589793/180;
        QPointF q(36+27*std::cos(a),36-27*std::sin(a));
        p.setBrush(isEnabled()?t->accent:t->muted);p.setPen(Qt::NoPen);p.drawEllipse(q,3,3);
        if(hasFocus()) {p.setBrush(Qt::NoBrush);p.setPen(QPen(t->accent,2,Qt::DashLine));p.drawRoundedRect(QRectF(1,1,70,70),7,7);}
    }
};

class CommandRow : public QAbstractButton {
public:
    Theme* t; Command c; int index; bool selected=false;bool detailOpen=false;
    std::function<void()> toggle;
    bool togglePressed=false;
    CommandRow(Theme* theme, Command cmd, int i, QWidget* parent=nullptr):QAbstractButton(parent),t(theme),c(cmd),index(i) {
        setFixedHeight(t->soft?68:40);setCursor(Qt::PointingHandCursor);setFocusPolicy(Qt::StrongFocus);
        setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Fixed);setMouseTracking(true);
        setAccessibleName(QString::number(i+1)+" "+c.title+" "+raw(c));
    }
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);p.setRenderHints(QPainter::Antialiasing|QPainter::TextAntialiasing);
        if(t->soft){paintSoftRow(p,rect(),*t,c,index,selected,detailOpen,underMouse(),hasFocus());return;}
        if(selected || underMouse()) {
            p.setBrush(selected?t->selected:t->well);p.setPen(Qt::NoPen);
            p.drawRoundedRect(rect().adjusted(0,1,0,-1),t->soft?8:3,t->soft?8:3);
        }
        if(selected) {p.fillRect(QRect(0,7,3,26),t->accent);}
        else if(!t->soft) {p.setPen(t->line);p.drawLine(16,height()-1,width()-16,height()-1);}
        QColor ink=c.on?t->ink:t->muted;
        // Only selection receives accent. On/off remains a legible neutral shape.
        p.setPen(QPen(selected?t->accent:t->muted,1.5));
        QPointF a(18,17),b(22,21),d(26,17);
        if(!detailOpen) {a=QPointF(20,15);b=QPointF(24,19);d=QPointF(20,23);}
        p.drawLine(a,b);p.drawLine(b,d);
        ::text(p,{36,0,30,40},QString::number(index+1).rightJustified(2,'0'),t->muted,13,false,Qt::AlignCenter,true);
        if(c.kind!="COMMENT") {
            QRectF box(78,12,15,15);p.setPen(QPen(t->muted,1.2));p.setBrush(c.on?t->ink:Qt::transparent);p.drawRoundedRect(box,3,3);
            if(c.on) {p.setPen(QPen(t->surface,1.5));p.drawLine(QPointF(81,19),QPointF(84,22));p.drawLine(QPointF(84,22),QPointF(90,16));}
        }
        ::text(p,{110,0,198,40},c.title,ink,15,selected);
        if(detailOpen) {
            // Expanded rows show values only in the editor, not twice.
        } else if(c.hz>0 || c.kind=="PRE") {
            const int start=340;
            const int step=qMin(230,qMax(150,int((width()-start-132)/3.)));
            if(c.hz>0) ::text(p,{double(start),0,double(step),40},number(c.hz,0)+" Hz",ink,14,false,Qt::AlignLeft|Qt::AlignVCenter,true);
            if(c.kind!="HP" && c.kind!="AP") ::text(p,{double(start+step),0,double(step),40},number(c.gain,1,true)+" dB",ink,14,false,Qt::AlignLeft|Qt::AlignVCenter,true);
            if(c.q>0) ::text(p,{double(start+step*2),0,double(step),40},"Q "+number(c.q,2),t->muted,14,false,Qt::AlignLeft|Qt::AlignVCenter,true);
        } else {
            QString detail=fontMetrics().elidedText(c.detail,Qt::ElideRight,width()-440);
            ::text(p,{340,0,double(width()-470),40},detail,t->muted,14);
        }
        if(!c.on) ::text(p,{double(width()-104),0,88,40},"꺼짐",t->muted,13,false,Qt::AlignRight|Qt::AlignVCenter);
        else if(selected) ::text(p,{double(width()-104),0,88,40},"편집 중",t->accent,13,false,Qt::AlignRight|Qt::AlignVCenter);
        if(hasFocus()) {p.setBrush(Qt::NoBrush);p.setPen(QPen(t->accent,1,Qt::DashLine));p.drawRoundedRect(rect().adjusted(4,3,-4,-3),3,3);}
    }
    void enterEvent(QEnterEvent*) override {update();}
    void leaveEvent(QEvent*) override {update();}
    void mousePressEvent(QMouseEvent* event) override {
        togglePressed=event->button()==Qt::LeftButton && QRect(68,t->soft?14:0,40,40).contains(event->position().toPoint()) && c.kind!="COMMENT";
        if(togglePressed){setDown(true);event->accept();return;}
        QAbstractButton::mousePressEvent(event);
    }
    void mouseReleaseEvent(QMouseEvent* event) override {
        const bool overToggle=QRect(68,t->soft?14:0,40,40).contains(event->position().toPoint());
        if(event->button()==Qt::LeftButton && (togglePressed || overToggle) && toggle && c.kind!="COMMENT") {
            const bool activate=togglePressed && overToggle;togglePressed=false;setDown(false);if(activate)toggle();event->accept();return;
        }
        QAbstractButton::mouseReleaseEvent(event);
    }
};

class Response : public QWidget {
public:
    Theme* t;bool stale=false;
    QString unavailable;
    bool studyUnits=false;
    explicit Response(Theme* theme):t(theme){setMinimumHeight(118);setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Expanding);}
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);p.setRenderHints(QPainter::Antialiasing|QPainter::TextAntialiasing);
        p.fillRect(rect(),t->soft?t->surface:t->well);
        if(!unavailable.isEmpty()) {text(p,rect().adjusted(20,20,-20,-20),unavailable,t->muted,14,false,Qt::AlignCenter);return;}
        QRectF area(50,14,width()-96,height()-44);
        if(studyUnits)text(p,{50,0,40,14},"dB",t->muted,11);
        const auto fx=[&](double f){return area.left()+std::log10(f/20)/3*area.width();};
        const auto fy=[&](double db){return area.top()+(12-db)/24*area.height();};
        for(double db : {12.,6.,0.,-6.,-12.}) {
            if(!t->soft || db==0 || std::abs(db)==12){p.setPen(QPen(db==0?t->muted:t->line,db==0?1.1:.7));p.drawLine(QPointF(area.left(),fy(db)),QPointF(area.right(),fy(db)));}
            text(p,{4,fy(db)-9,36,18},(db>0?"+":"")+QString::number(db),t->muted,t->soft?12:11,false,Qt::AlignRight|Qt::AlignVCenter,!t->soft);
        }
        for(double f : {20.,50.,100.,200.,500.,1000.,2000.,5000.,10000.,20000.}) {
            if(!t->soft || f==100 || f==1000 || f==10000){p.setPen(QPen(t->line,.7));p.drawLine(QPointF(fx(f),area.top()),QPointF(fx(f),area.bottom()));}
            text(p,{fx(f)-25,area.bottom()+5,50,20},f>=1000?QString::number(f/1000)+"k":QString::number(f),t->muted,t->soft?12:11,false,Qt::AlignCenter,!t->soft);
        }
        // Illustrative trace transcribed from #341's baseline screenshot, NOT DSP.
        const QVector<QPointF> samples={{20,-9},{30,-3.9},{50,-.1},{80,.8},{100,.6},{150,-2},{250,-6.2},{400,-3.2},{800,-.3},{1000,-.4},{1500,-1.5},{3000,-4.9},{5000,-3.1},{10000,-2.5},{20000,-2.5}};
        QPainterPath path;
        for(int i=0;i<samples.size();++i) {
            QPointF q(fx(samples[i].x()),fy(samples[i].y()));
            if(i==0) path.moveTo(q);
            else {
                QPointF prev(fx(samples[i-1].x()),fy(samples[i-1].y()));
                double dx=(q.x()-prev.x())*.45;path.cubicTo(prev+QPointF(dx,0),q-QPointF(dx,0),q);
            }
        }
        p.setClipRect(area.adjusted(-2,-2,2,2));
        p.setPen(t->soft?QPen(stale?t->muted:t->accent,2.8,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin):QPen(stale?t->muted:t->accent,2.2));
        p.setBrush(Qt::NoBrush);p.drawPath(path);p.setClipping(false);
        text(p,{double(width()-44),double(height()-24),42,20},"Hz",t->muted,11,false,Qt::AlignRight|Qt::AlignVCenter);
    }
};

#include "StudySurfaces.h"

class Mockup : public QWidget {
public:
    bool study=false;
    QString repoRoot;
    QString activeFile="config.txt";
    QMap<QString,QVector<Command>> documents;
    QMap<QString,bool> documentDirty;
    QMap<QString,QVector<QVector<Command>>> undoByFile,redoByFile;
    QVector<Command> studyCommitted;
    QSet<int> expandedStudyRows;
    bool instantPreview=true,analysisVisible=true;
    QString analysisMetric="크기",analysisChannel="L",analysisResolution="262144";
    bool includeDelay=false;
    QWidget* studyOverlay=nullptr;
    QLineEdit* pickerSearch=nullptr;QListWidget* pickerList=nullptr;
    QPushButton* pickerAdd=nullptr;
    int insertionIndex=0;
    QAction *undoAction=nullptr,*redoAction=nullptr;
    GraphicStudyPlot* graphicPlot=nullptr;
    void beginStudy(QString file="config.txt");
    void loadStudyFile(QString file);
    void recordStudyChange();
    void undoStudy(bool redo=false);
    void buildSoftStudy();void buildStudioStudy();
    void styleStudy();
    QWidget* studyChrome();QWidget* studyTabs();
    QWidget* studyList();QWidget* studyAnalysis();QWidget* studyFooter();
    void makeStudyBody(QVBoxLayout* layout,int index);
    QWidget* referenceBody(int index);QWidget* routingBody(int index);QWidget* graphicBody(int index);QWidget* scopeBody(int index);
    void showPicker(int index);void filterPicker(QString text);void insertPicked();void insertStudyCommand(Command command,int index);
    void deleteStudyCommand();void showFileChoice(int index=-1);void showAnalysisSettings();void closeStudyOverlay();
    QWidget* createStudyOverlay(QString title,QSize size);
    QPushButton* iconButton(QString icon,QString label,std::function<void()> callback);
    QIcon studyIcon(QString name,QColor tint) const;
    QStringList studyFiles() const;
    void rebuildSoft();
    void makeSoftEditor(QVBoxLayout* group);
    Theme t=themeFor("studio-dark");
    QVector<Command> commands=fixture();
    int selected=3;bool expanded=true;bool dirty=false;bool code=false;
    QString themeName="studio-dark";
    QVBoxLayout* mainLayout=nullptr;QVBoxLayout* rowsLayout=nullptr;QScrollArea* scroll=nullptr;
    QVector<CommandRow*> rows;QVector<QDoubleSpinBox*> values;QVector<Knob*> knobs;
    QLabel *status=nullptr,*traceNote=nullptr;Response* response=nullptr;QPushButton* enabled=nullptr;
    QWidget* editor=nullptr;
    QWidget* softAnalysis=nullptr;

    Mockup() {
        setWindowTitle("EqualizerAPO-XT · Issue 341 design prototype");setMinimumSize(1120,800);resize(1600,1000);
        rebuild();
    }
    void paintEvent(QPaintEvent*) override {QPainter p(this);p.fillRect(rect(),t.window);}
    void resizeEvent(QResizeEvent* event) override {
        QWidget::resizeEvent(event);
        if(study && mainLayout) {
            mainLayout->setContentsMargins(t.soft?28:16,t.soft?8:4,t.soft?28:16,t.soft?8:6);
            if(studyOverlay){studyOverlay->setGeometry(rect());if(auto panel=studyOverlay->findChild<QWidget*>("studyOverlayPanel",Qt::FindDirectChildrenOnly))panel->move((width()-panel->width())/2,(height()-panel->height())/2);}
            return;
        }
        if(t.soft && mainLayout) {
            const int side=qMax(28,(width()-1240)/2);mainLayout->setContentsMargins(side,12,side,8);
            if(softAnalysis)softAnalysis->setFixedHeight(height()<960?188:208);
        }
    }
    QLabel* label(QString s, bool muted=false, int size=15) {
        auto w=new QLabel(s);w->setFont(face(size));w->setProperty("muted",muted);return w;
    }
    QPushButton* button(QString s, std::function<void()> fn={}) {
        if(study && !fn && s=="+ 앞에 삽입")fn=[this]{showPicker(selected);};
        if(study && !fn && s=="삭제")fn=[this]{deleteStudyCommand();};
        auto b=new QPushButton(s);b->setMinimumHeight(40);b->setCursor(Qt::PointingHandCursor);
        if(fn) connect(b,&QPushButton::clicked,this,fn);else {b->setEnabled(false);b->setToolTip("설계 목업의 표시 전용 항목입니다.");}
        return b;
    }
    QHBoxLayout* horizontal(QWidget* w,int margin=0) {
        auto l=new QHBoxLayout(w);l->setContentsMargins(margin,0,margin,0);l->setSpacing(12);return l;
    }
    void style() {
        QPalette palette;
        palette.setColor(QPalette::Window,t.window);palette.setColor(QPalette::Base,t.surface);
        palette.setColor(QPalette::WindowText,t.ink);palette.setColor(QPalette::Text,t.ink);
        palette.setColor(QPalette::Button,t.well);palette.setColor(QPalette::ButtonText,t.ink);
        palette.setColor(QPalette::Highlight,t.selected);palette.setColor(QPalette::HighlightedText,t.ink);
        qApp->setPalette(palette);
        QString s=QString(R"(
            QLabel {color:%1; background:transparent;}
            QLabel[muted="true"] {color:%2;}
            QPushButton {color:%1;background:%3;border:1px solid %4;border-radius:%5px;padding:0 14px;font-family: 'EAPO Sans KR';font-size:14px;}
            QPushButton:hover {background:%6;}
            QPushButton:disabled {color:%2;background:transparent;border-color:transparent;}
            QPushButton:focus {border:2px solid %7;}
            QPushButton:checked {background:%6;color:%7;border-color:%7;}
            QComboBox {color:%1;background:%3;border:1px solid %4;border-radius:%5px;padding:8px 12px;font-size:14px;}
            QComboBox QAbstractItemView {background:%3;color:%1;selection-background-color:%6;}
            QDoubleSpinBox {color:%1;background:%3;border:1px solid %4;border-radius:%5px;padding:7px 12px;}
            QDoubleSpinBox:focus {border:2px solid %7;}
            QDoubleSpinBox:disabled {color:%2;}
            QScrollArea {border:0;background:transparent;}
            QScrollBar:vertical {background:%3;width:8px;}
            QScrollBar::handle:vertical {background:%4;min-height:40px;border-radius:4px;}
            QScrollBar::add-line:vertical,QScrollBar::sub-line:vertical {height:0;}
            QToolTip {color:%1;background:%3;border:1px solid %4;padding:6px;}
        )").arg(t.ink.name(),t.muted.name(),t.well.name(),t.line.name(),QString::number(t.soft?16:6),t.selected.name(),t.accent.name());
        if(t.soft)s+=QString(R"(
            QPushButton {border-color:transparent;border-radius:20px;background:%1;padding:0 18px;font-size:14px;}
            QPushButton:checked {border-color:transparent;background:%2;color:%3;}
            QPushButton:focus {border:2px solid %3;}
            QDoubleSpinBox {background:%1;border:1px solid transparent;border-radius:22px;padding:4px 10px;}
            QDoubleSpinBox:hover {border-color:%2;}
            QDoubleSpinBox:focus {border:2px solid %3;}
            QLabel[warning="true"] {color:%4;}
        )").arg(t.well.name(),t.selected.name(),t.accent.name(),t.warning.name());
        qApp->setStyleSheet(s);
    }
    void rebuild() {
        if(study){closeStudyOverlay();recordStudyChange();}
        if(mainLayout) {
            while(auto item=mainLayout->takeAt(0)) {delete item->widget();delete item;}
            delete mainLayout;
            mainLayout=nullptr;
        }
        rows.clear();values.clear();knobs.clear();editor=nullptr;softAnalysis=nullptr;
        undoAction=nullptr;redoAction=nullptr;
        if(study){style();styleStudy();if(t.soft)buildSoftStudy();else buildStudioStudy();return;}
        if(t.soft){style();rebuildSoft();return;}
        style();mainLayout=new QVBoxLayout(this);mainLayout->setContentsMargins(20,0,20,0);mainLayout->setSpacing(10);
        auto top=new QWidget;auto topL=horizontal(top);top->setFixedHeight(38);
        auto brand=label("Equalizer APO XT",false,15);brand->setFont(face(15,true));topL->addWidget(brand);
        topL->addSpacing(14);topL->addWidget(label("구성 편집기",true,13));topL->addStretch();
        topL->addWidget(label("#341  ·  디자인 목업",true,12));
        auto theme=new QComboBox;theme->addItems({"Studio · 다크","Studio · 라이트","Soft · 다크","Soft · 라이트"});theme->setAccessibleName("목업 테마");
        QStringList names={"studio-dark","studio-light","soft-dark","soft-light"};theme->setCurrentIndex(names.indexOf(themeName));topL->addWidget(theme);
        connect(theme,&QComboBox::currentIndexChanged,this,[this,names](int i){QTimer::singleShot(0,this,[this,names,i]{themeName=names[i];t=themeFor(themeName);rebuild();});});
        mainLayout->addWidget(top);

        auto toolbar=new QWidget;auto tl=horizontal(toolbar);toolbar->setFixedHeight(46);
        auto filename=label("config.txt",false,18);filename->setFont(face(18,true));tl->addWidget(filename);tl->addSpacing(10);
        tl->addWidget(button("초기 상태로",[this]{commands=fixture();selected=3;dirty=false;expanded=true;code=false;rebuild();}));
        status=label(dirty?"목업 값 변경됨":"원본 예제",true,13);tl->addWidget(status);tl->addStretch();
        tl->addWidget(label("장치",true,13));tl->addWidget(label("CABLE Input · VB-Audio Virtual Cable",false,14));
        tl->addSpacing(16);tl->addWidget(label("스테레오  /  48 kHz",true,13));mainLayout->addWidget(toolbar);

        auto listSurface=new Surface(&t);auto listL=new QVBoxLayout(listSurface);listL->setContentsMargins(10,0,10,5);listL->setSpacing(0);
        auto listHead=new QWidget;auto lh=horizontal(listHead,16);listHead->setFixedHeight(38);
        lh->addWidget(label("처리 순서",false,14));lh->addSpacing(12);lh->addWidget(label("위에서 아래로 적용",true,12));lh->addStretch();lh->addWidget(label("채널 범위  L · R",true,13));
        listL->addWidget(listHead);
        scroll=new QScrollArea;scroll->setWidgetResizable(true);scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);scroll->setFrameShape(QFrame::NoFrame);
        scroll->viewport()->setAutoFillBackground(false);
        auto contents=new QWidget;contents->setAutoFillBackground(false);rowsLayout=new QVBoxLayout(contents);rowsLayout->setContentsMargins(0,0,0,0);rowsLayout->setSpacing(0);
        for(int i=0;i<commands.size();++i) {
            auto row=new CommandRow(&t,commands[i],i);row->selected=i==selected;row->detailOpen=i==selected && expanded;rows.append(row);rowsLayout->addWidget(row);
            row->toggle=[this,i]{commands[i].on=!commands[i].on;dirty=true;QTimer::singleShot(0,this,[this]{rebuild();});};
            connect(row,&QAbstractButton::clicked,this,[this,i]{QTimer::singleShot(0,this,[this,i]{selectRow(i);});});
            if(i==selected && expanded) makeEditor();
        }
        rowsLayout->addStretch(1);scroll->setWidget(contents);
        QPalette cp=contents->palette();cp.setColor(QPalette::Window,t.surface);contents->setPalette(cp);contents->setAutoFillBackground(true);
        listL->addWidget(scroll,1);mainLayout->addWidget(listSurface,1);

        auto analysis=new Surface(&t);analysis->setFixedHeight(210);auto al=new QVBoxLayout(analysis);al->setContentsMargins(16,0,16,10);al->setSpacing(0);
        auto graphHead=new QWidget;graphHead->setFixedHeight(40);auto gl=horizontal(graphHead);
        gl->addWidget(label("합산 응답",false,15));gl->addSpacing(12);gl->addWidget(label("크기 (dB) · L",true,13));gl->addStretch();
        traceNote=label(dirty?"값 변경됨 · 응답 재계산 안 됨":"응답 예시 · 원본 캡처 기준",true,12);gl->addWidget(traceNote);
        gl->addSpacing(20);auto warning=label("△  예시 피크 +0.8 dB · 0 dB 초과",false,13);QPalette wp=warning->palette();wp.setColor(QPalette::WindowText,t.warning);warning->setPalette(wp);
        // Stylesheet label color overrides palettes; use the shared property selector.
        warning->setProperty("warning",true);gl->addWidget(warning);qApp->setStyleSheet(qApp->styleSheet()+QString("QLabel[warning=\"true\"] {color:%1;}").arg(t.warning.name()));
        al->addWidget(graphHead);response=new Response(&t);response->stale=dirty;al->addWidget(response,1);mainLayout->addWidget(analysis);
        auto foot=new QWidget;foot->setFixedHeight(24);auto fl=horizontal(foot);
        fl->addWidget(label("선택 행의 체크 표시: 사용 여부  ·  파랑: 현재 편집 위치",true,11));fl->addStretch();fl->addWidget(label("오디오 엔진 미연결 · 설정 파일을 저장하지 않습니다",true,11));mainLayout->addWidget(foot);
        update();
    }
    void selectRow(int i) {
        if(i<0 || i>=commands.size()) return;
        int oldScroll=scroll->verticalScrollBar()->value();
        if(study){if(selected==i && expandedStudyRows.contains(i))expandedStudyRows.remove(i);else expandedStudyRows.insert(i);selected=i;expanded=expandedStudyRows.contains(i);code=false;}
        else if(selected==i) expanded=!expanded;else {selected=i;expanded=true;code=false;}
        rebuild();qApp->processEvents();scroll->verticalScrollBar()->setValue(oldScroll);
        scroll->ensureWidgetVisible(rows[i],0,8);
        if(t.soft && expanded && editor) {
            const QPoint bottom=editor->mapTo(scroll->widget(),QPoint(0,editor->height()));
            scroll->ensureVisible(bottom.x(),bottom.y(),0,8);
        }
    }
    void makeEditor() {
        editor=new QWidget;auto el=new QVBoxLayout(editor);el->setContentsMargins(110,2,20,12);el->setSpacing(8);
        Command c=commands[selected];
        if(c.hz>0 || c.kind=="PRE") {
            auto params=new QWidget;params->setMaximumWidth(1040);auto pl=horizontal(params);pl->setSpacing(30);
            auto param=[&](QString title,QString unit,double value,double min,double max,int decimals,QString role){
                auto block=new QWidget;auto bl=horizontal(block);bl->setSpacing(14);
                const bool logarithmic=role!="gain";
                const auto toDial=[=](double v){return qRound((logarithmic?std::log(v/min)/std::log(max/min):(v-min)/(max-min))*1000);};
                const auto fromDial=[=](int n){return logarithmic?min*std::pow(max/min,n/1000.):min+(max-min)*n/1000.;};
                auto knob=new Knob(&t);knob->bipolar=role=="gain";knob->setRange(0,1000);knob->setValue(toDial(value));knob->setAccessibleName(title+" 노브");knob->setEnabled(c.on);bl->addWidget(knob);knobs.append(knob);
                auto fields=new QWidget;auto vl=new QVBoxLayout(fields);vl->setContentsMargins(0,0,0,0);vl->setSpacing(5);vl->addWidget(label(title,true,13));
                auto spin=new QDoubleSpinBox;spin->setRange(min,max);spin->setDecimals(decimals);spin->setValue(value);spin->setSuffix(" "+unit);spin->setFont(face(20,true,true));spin->setMinimumHeight(43);spin->setMinimumWidth(150);spin->setButtonSymbols(QAbstractSpinBox::NoButtons);spin->setSingleStep(role=="hz"?1:.1);spin->setAccessibleName(title);spin->setEnabled(c.on);spin->setProperty("role",role);values.append(spin);vl->addWidget(spin);bl->addWidget(fields,1);pl->addWidget(block,1);
                if(role=="gain" && value>0)spin->setPrefix("+");
                connect(spin,qOverload<double>(&QDoubleSpinBox::valueChanged),this,[=](double n){
                    if(role=="hz")commands[selected].hz=n;else if(role=="gain")commands[selected].gain=n;else commands[selected].q=n;
                    if(role=="gain")spin->setPrefix(n>0?"+":"");
                    QSignalBlocker lock(knob);knob->setValue(toDial(n));changed();
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
            if(c.kind=="INCLUDE")detail="example.txt  ·  내부 명령 2개: 프리앰프 −2 dB / 피킹 3,000 Hz, −3 dB, Q 1";
            if(c.kind=="CH")detail="L · R 선택이 아래 명령에 적용됩니다. 다음 Channel 명령에서 범위가 바뀝니다.";
            auto d=label(detail,false,15);d->setWordWrap(true);d->setMinimumHeight(60);el->addWidget(d);
        }
        auto actions=new QWidget;auto ac=horizontal(actions);ac->setSpacing(8);
        enabled=button(c.on?"✓ 사용 중":"사용 안 함",[this]{commands[selected].on=!commands[selected].on;dirty=true;QTimer::singleShot(0,this,[this]{rebuild();});});enabled->setCheckable(true);enabled->setChecked(c.on);enabled->setAccessibleName("선택 명령 사용 여부");ac->addWidget(enabled);
        if(c.kind=="COMMENT"){enabled->setText("주석");enabled->setChecked(false);enabled->setEnabled(false);}
        ac->addWidget(button("원문 보기",[this]{code=!code;QTimer::singleShot(0,this,[this]{rebuild();});}));
        ac->addWidget(button("+ 앞에 삽입"));ac->addWidget(button("삭제"));ac->addStretch();
        ac->addWidget(label(c.on?"적용 채널  L · R":"꺼진 명령 · 값은 유지됩니다",true,13));el->addWidget(actions);
        if(code) {auto source=label(raw(c),false,14);source->setTextInteractionFlags(Qt::TextSelectableByMouse);source->setFont(face(14,false,true));source->setWordWrap(true);el->addWidget(source);}
        rowsLayout->addWidget(editor);
    }
    void changed() {
        if(study)recordStudyChange();
        dirty=true;rows[selected]->c=commands[selected];rows[selected]->update();status->setText("목업 값 변경됨");
        if(traceNote)traceNote->setText("값 변경됨 · 응답 재계산 안 됨");if(response){response->stale=true;response->update();}
    }
};

#include "SoftLayout.h"
#include "EditorStudy.h"
#include "StudyEditors.h"
#include "SoftEditorStudy.h"
#include "StudioEditorStudy.h"

static bool save(Mockup& w,const QString& dir,QString name,QSize size=QSize(1600,1000),double dpr=1) {
    w.resize(size);w.show();qApp->processEvents();
    for(QWidget* child:w.findChildren<QWidget*>()) {child->clearFocus();QEvent leave(QEvent::Leave);QApplication::sendEvent(child,&leave);}
    w.scroll->verticalScrollBar()->setValue(0);qApp->processEvents();
    if(w.selected>7 && w.editor)w.scroll->ensureWidgetVisible(w.editor,0,8);
    if(w.t.soft && w.editor){const QPoint bottom=w.editor->mapTo(w.scroll->widget(),QPoint(0,w.editor->height()));w.scroll->ensureVisible(bottom.x(),bottom.y(),0,8);}
    QPixmap pix(QSize(qRound(size.width()*dpr),qRound(size.height()*dpr)));pix.setDevicePixelRatio(dpr);pix.fill(w.t.window);
    w.render(&pix);return pix.save(dir+"/"+name+".png");
}

#include "StudyScenarios.h"

int main(int argc,char** argv) {
    QApplication app(argc,argv);app.setStyle("Fusion");
    QStringList args=app.arguments();QString repo=QDir::currentPath();int r=args.indexOf("--repo");if(r>=0 && r+1<args.size())repo=args[r+1];
    for(QString file:{"Pretendard-Regular.otf","Pretendard-Medium.otf","Pretendard-SemiBold.otf","DMMono-Regular.ttf","DMMono-Medium.ttf"}) QFontDatabase::addApplicationFont(repo+"/Editor/fonts/"+file);
    app.setFont(face());Mockup w;
    w.repoRoot=repo;
    int themeArg=args.indexOf("--theme");
    if(themeArg>=0 && themeArg+1<args.size()) {
        const QString value=args[themeArg+1];
        if(!QStringList{"studio-dark","studio-light","soft-dark","soft-light"}.contains(value))return 2;
        w.themeName=value;w.t=themeFor(value);w.rebuild();
    }
    if(args.contains("--study"))w.beginStudy();
    if(args.contains("--render-study") || args.contains("--test-study"))return runStudyScenarios(w,args);
    if(args.contains("--test")) {
        int fails=0;auto check=[&](bool ok,const char* msg){QTextStream(stdout)<<(ok?"PASS ":"FAIL ")<<msg<<Qt::endl;if(!ok)++fails;};
        w.show();app.processEvents();check(w.commands.size()==13,"all 13 source lines retained");check(w.selected==3,"same selected LS row as baseline");
        check(w.values.size()==3,"three real value editors");
        check(w.knobs[0]->value()>200 && w.knobs[0]->value()<250,"frequency dial uses logarithmic range");
        check(w.knobs[1]->bipolar,"gain arc starts at zero detent");
        double before=w.commands[3].gain;w.values[1]->setValue(4);check(w.commands[3].gain==4 && before==3,"gain edits fixture only");check(w.response->stale,"graph explicitly marked stale after edit");
        QTest::mouseClick(w.enabled,Qt::LeftButton);app.processEvents();check(!w.commands[3].on && !w.values[0]->isEnabled(),"bypass keeps readable disabled values");
        QTest::mouseClick(w.rows[3],Qt::LeftButton,Qt::NoModifier,QPoint(85,20));app.processEvents();check(w.commands[3].on,"row checkbox has real 40px toggle target");
        w.selectRow(10);check(w.selected==10 && w.commands[10].kind=="INCLUDE","include can be inspected");
        w.selectRow(10);check(!w.expanded,"selected row collapses");
        w.selectRow(4);app.processEvents();w.rows[5]->setFocus();QTest::keyClick(w.rows[5],Qt::Key_Space);app.processEvents();check(w.selected==5,"keyboard row selection");
        w.values[1]->setFocus();QTest::keyClick(w.values[1],Qt::Key_Up);check(w.commands[5].gain>6,"keyboard increments selected gain");
        for(QString name:{"studio-dark","studio-light","soft-dark","soft-light"}) {
            w.themeName=name;w.t=themeFor(name);w.rebuild();w.resize(1280,900);app.processEvents();
            check(w.scroll->horizontalScrollBar()->maximum()==0,"1280 width has no horizontal scroll");
            for(auto* spin:w.values)check(spin->width()>=150 && spin->height()>=40,"readout hit area and number width");
        }
        const int widgets=w.findChildren<QWidget*>().size();
        for(int i=0;i<10;++i){w.rebuild();app.processEvents();}
        check(w.findChildren<QWidget*>().size()==widgets,"ten rebuilds do not accumulate child widgets");
        w.commands=fixture();w.selected=3;w.expanded=true;w.themeName="soft-light";w.t=themeFor(w.themeName);w.rebuild();w.resize(1120,800);app.processEvents();
        check(w.rows[0]->height()==68,"Soft has two-line 68px rows, not Studio rows");
        check(w.knobs[0]->size()==QSize(104,104),"Soft has full-size 104px tactile knobs");
        check(!w.values[0]->font().families().contains("EAPO Mono"),"Soft readouts use proportional UI type");
        for(auto* spin:w.values) {
            const QRect bounds(spin->mapTo(&w,QPoint()),spin->size());
            check(bounds.left()>=0 && bounds.right()<w.width(),"1120px Soft value controls stay inside window");
        }
        QTest::mouseClick(w.rows[3],Qt::LeftButton,Qt::NoModifier,QPoint(85,34));app.processEvents();check(!w.commands[3].on && !w.values[0]->isEnabled(),"Soft switch disables selected value controls");
        QTest::mouseClick(w.enabled,Qt::LeftButton);app.processEvents();check(w.commands[3].on && w.values[0]->isEnabled(),"Soft action restores enabled values");
        QTest::mousePress(w.rows[3],Qt::LeftButton,Qt::NoModifier,QPoint(300,20));QTest::mouseRelease(w.rows[3],Qt::LeftButton,Qt::NoModifier,QPoint(85,34));app.processEvents();check(w.commands[3].on,"releasing across a switch without pressing it cannot toggle");
        w.values[1]->setFocus();QTest::keyClick(w.values[1],Qt::Key_Up);check(w.commands[3].gain>3 && w.response->stale,"Soft keyboard edit marks response stale");
        w.selectRow(10);app.processEvents();check(w.selected==10 && w.editor,"Soft Include remains inspectable");
        check(w.commands.size()==13,"Soft preserves every fixture line");
        QTextStream(stdout)<<"RESULT "<<fails<<" failures"<<Qt::endl;return fails?1:0;
    }
    int render=args.indexOf("--render");
    if(render>=0 && render+1<args.size()) {
        QString out=args[render+1];QDir().mkpath(out);bool ok=true;
        for(QString name:{"studio-dark","studio-light","soft-dark","soft-light"}) {
            w.themeName=name;w.t=themeFor(name);w.rebuild();ok=save(w,out,name)&&ok;
        }
        w.themeName="studio-dark";w.t=themeFor(w.themeName);w.rebuild();ok=save(w,out,"studio-1280",QSize(1280,900))&&ok;
        ok=save(w,out,"studio-150pct",QSize(1280,900),1.5)&&ok;
        w.commands[3].on=false;w.dirty=true;w.rebuild();ok=save(w,out,"studio-bypassed")&&ok;
        w.commands[3].on=true;w.dirty=false;w.selected=10;w.expanded=true;w.code=true;w.rebuild();ok=save(w,out,"studio-include")&&ok;
        w.selected=3;w.code=true;w.rebuild();ok=save(w,out,"studio-source")&&ok;
        w.themeName="soft-light";w.t=themeFor(w.themeName);w.selected=3;w.code=false;w.rebuild();ok=save(w,out,"soft-light-1280",QSize(1280,900))&&ok;
        ok=save(w,out,"soft-light-150pct",QSize(1280,900),1.5)&&ok;
        w.commands[3].on=false;w.dirty=true;w.rebuild();ok=save(w,out,"soft-light-bypassed")&&ok;
        w.commands[3].on=true;w.dirty=false;w.selected=10;w.code=true;w.rebuild();ok=save(w,out,"soft-light-include")&&ok;
        QTextStream(stdout)<<"RENDER "<<(ok?"PASS ":"FAIL ")<<out<<Qt::endl;return ok?0:1;
    }
    w.show();return app.exec();
}
