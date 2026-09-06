// Soft-only geometry and painting. The shared command model is unchanged.
#pragma once

static QColor softMix(QColor a,QColor b,double amount) {
    return QColor::fromRgbF(a.redF()*(1-amount)+b.redF()*amount,
        a.greenF()*(1-amount)+b.greenF()*amount,a.blueF()*(1-amount)+b.blueF()*amount);
}
static QFont softFont(int size=16,bool strong=false) {
    auto f=face(size,strong,false);
    f.setFeature(QFont::Tag("tnum"),1);
    return f;
}
static QString softDescription(const Command& c) {
    if(c.kind=="VST")return "외부 플러그인과 입출력 버스를 설정합니다";
    if(c.kind=="CONV")return "임펄스 응답 파일을 적용합니다";
    if(c.kind=="DEV")return "이후 명령을 적용할 장치를 제한합니다";
    if(c.kind=="STAGE")return "이후 명령의 처리 단계를 선택합니다";
    if(c.kind=="CH")return "이후 명령이 적용될 채널을 선택합니다";
    if(c.kind=="PRE")return "전체 음량을 조절합니다";
    if(c.kind=="HP")return "기준 주파수 아래의 소리를 줄입니다";
    if(c.kind=="LS")return "낮은음의 크기를 부드럽게 조절합니다";
    if(c.kind=="PK")return "특정 주파수 주변의 크기를 조절합니다";
    if(c.kind=="AP")return "크기는 유지하고 위상을 바꿉니다";
    if(c.kind=="HS")return "높은음의 크기를 조절합니다";
    if(c.kind=="COPY")return "채널의 신호를 섞거나 복사합니다";
    if(c.kind=="DELAY")return "신호의 시간을 조절합니다";
    if(c.kind=="INCLUDE")return "다른 설정 파일을 이 위치에 적용합니다";
    if(c.kind=="GEQ")return "여러 주파수 지점의 크기를 조절합니다";
    return c.detail;
}
static QString softSummary(const Command& c) {
    if(c.kind=="PRE")return number(c.gain,1,true)+" dB";
    if(c.hz>0) {
        QString s=number(c.hz,0)+" Hz";
        if(c.kind!="HP" && c.kind!="AP")s+="  ·  "+number(c.gain,1,true)+" dB";
        return s+="  ·  Q "+number(c.q,2);
    }
    return c.kind=="COMMENT"?QString():c.detail;
}
static void softPanel(QPainter& p,QRectF r,const Theme& t,bool selected=false,bool off=false) {
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);p.setBrush(softMix(t.window,t.line,.24));
    p.drawRoundedRect(r.adjusted(0,2,0,0),18,18);
    const QColor fill=off?t.window:(selected?softMix(t.surface,t.selected,.25):t.surface);
    p.setBrush(fill);
    p.setPen(QPen(selected?softMix(t.surface,t.accent,.42):softMix(t.surface,t.line,.3),1,off?Qt::DashLine:Qt::SolidLine));
    p.drawRoundedRect(r.adjusted(0,0,0,-2),18,18);
}
class SoftPanel : public QWidget {
public:
    Theme* t;bool selected=false;bool off=false;
    explicit SoftPanel(Theme* theme):t(theme){setAutoFillBackground(false);}
    void paintEvent(QPaintEvent*) override {QPainter p(this);softPanel(p,QRectF(rect()).adjusted(1,1,-2,-3),*t,selected,off);}
};
static void paintSoftRow(QPainter& p,QRectF r,const Theme& t,const Command& c,int index,bool selected,bool detailOpen,bool hovered,bool focus) {
    if(!detailOpen)softPanel(p,r.adjusted(1,1,-2,-3),t,selected,!c.on);
    if(hovered && !selected) {
        p.setPen(Qt::NoPen);p.setBrush(softMix(t.surface,t.selected,.42));p.drawRoundedRect(r.adjusted(3,3,-4,-6),16,16);
    }
    p.setPen(QPen(t.muted,1.8,Qt::SolidLine,Qt::RoundCap));
    double cy=r.height()/2;
    if(detailOpen){p.drawLine(QPointF(21,cy-2),QPointF(25,cy+2));p.drawLine(QPointF(25,cy+2),QPointF(29,cy-2));}
    else {p.drawLine(QPointF(23,cy-4),QPointF(27,cy));p.drawLine(QPointF(27,cy),QPointF(23,cy+4));}
    text(p,{39,0,29,r.height()},QString::number(index+1),t.muted,14,false,Qt::AlignCenter);
    if(c.kind!="COMMENT") {
        // Neutral switches carry enabled state; only the selected switch is blue.
        QColor track=c.on?(selected?softMix(t.surface,t.accent,.8):softMix(t.surface,t.muted,.8)):t.line;
        p.setBrush(track);p.setPen(Qt::NoPen);p.drawRoundedRect(QRectF(73,cy-10,32,20),10,10);
        p.setBrush(t.surface);p.drawEllipse(QPointF(c.on?96:82,cy),7,7);
    }
    QColor ink=c.on?t.ink:t.muted;
    text(p,{125,11,500,24},c.title,ink,17,true);
    text(p,{125,37,580,22},softDescription(c),t.muted,14);
    if(!detailOpen) {
        p.setFont(softFont(15));p.setPen(c.on?t.ink:t.muted);
        QString s=softSummary(c);if(!c.on)s="꺼짐  ·  "+s;
        const QRectF summary(r.width()*.57,0,r.width()*.43-28,r.height());
        p.drawText(summary,Qt::AlignRight|Qt::AlignVCenter,p.fontMetrics().elidedText(s,Qt::ElideRight,int(summary.width())));
    } else if(selected) {
        QRectF chip(r.right()-115,20,84,28);p.setPen(Qt::NoPen);p.setBrush(t.selected);p.drawRoundedRect(chip,14,14);
        text(p,chip,c.on?"조절 중":"사용 안 함",t.accent,13,false,Qt::AlignCenter);
    }
    if(focus) {p.setBrush(Qt::NoBrush);p.setPen(QPen(t.accent,2,Qt::DashLine));p.drawRoundedRect(r.adjusted(4,4,-5,-7),15,15);}
}
static void paintSoftKnob(QPainter& p,const Theme& t,QSize size,double v,bool bipolar,bool enabled,bool focused) {
    p.setRenderHints(QPainter::Antialiasing|QPainter::TextAntialiasing);
    const double center=size.width()/2.;p.translate(center,center);
    const QRectF ring(-43,-43,86,86);
    const QColor accent=enabled?softMix(t.surface,t.accent,.78):t.muted;
    p.setBrush(Qt::NoBrush);p.setPen(QPen(softMix(t.surface,t.accent,.17),6,Qt::SolidLine,Qt::RoundCap));p.drawArc(ring,225*16,-270*16);
    p.setPen(QPen(accent,6,Qt::SolidLine,Qt::RoundCap));
    p.drawArc(ring,bipolar?90*16:225*16,int(-270*16*(bipolar?v-.5:v)));
    p.setPen(Qt::NoPen);p.setBrush(softMix(t.surface,t.line,.73));p.drawEllipse(QRectF(-34,-30,68,68));
    p.setBrush(softMix(t.surface,t.well,.65));p.drawEllipse(QRectF(-34,-34,68,68));
    p.setPen(QPen(softMix(t.surface,t.line,.5),1));p.setBrush(t.surface);p.drawEllipse(QRectF(-32,-34,64,64));
    const double angle=(225-270*v)*3.141592653589793/180;
    p.setBrush(accent);p.setPen(Qt::NoPen);p.drawEllipse(QPointF(24*std::cos(angle),-24*std::sin(angle)),4.2,4.2);
    if(bipolar){p.setPen(QPen(t.muted,1.7,Qt::SolidLine,Qt::RoundCap));p.drawLine(QPointF(0,-48),QPointF(0,-43));}
    if(focused){p.setPen(QPen(t.accent,2,Qt::DashLine));p.setBrush(Qt::NoBrush);p.drawEllipse(QRectF(-50,-50,100,100));}
}
