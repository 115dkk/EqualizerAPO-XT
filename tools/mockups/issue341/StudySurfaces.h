#pragma once

class StudyElidedLabel : public QLabel {
public:
    Theme* t;StudyElidedLabel(Theme* theme,QString value):QLabel(value),t(theme){setObjectName("studyReferenceName");setToolTip(value);setAccessibleName(value);setMinimumWidth(30);setMaximumWidth(420);setSizePolicy(QSizePolicy::Preferred,QSizePolicy::Preferred);}
    QSize sizeHint()const override{return QSize(qBound(80,fontMetrics().horizontalAdvance(QLabel::text()),420),fontMetrics().height()+4);}
    QSize minimumSizeHint()const override{return QSize(30,fontMetrics().height()+4);}
    void paintEvent(QPaintEvent*)override{QPainter p(this);p.setRenderHint(QPainter::TextAntialiasing);p.setFont(font());p.setPen(t->ink);p.drawText(rect(),Qt::AlignLeft|Qt::AlignVCenter,fontMetrics().elidedText(QLabel::text(),Qt::ElideMiddle,width()));}
};

class StudyComboBox : public QComboBox {
public:
    Theme* t;explicit StudyComboBox(Theme* theme):t(theme){setMinimumHeight(40);setFont(face(14));}
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);p.setRenderHints(QPainter::Antialiasing|QPainter::TextAntialiasing);
        QRectF r=QRectF(rect()).adjusted(1,1,-1,-1);p.setBrush(t->well);p.setPen(QPen(hasFocus()?t->accent:t->line,hasFocus()?2:1));p.drawRoundedRect(r,t->soft?20:6,t->soft?20:6);
        p.setFont(face(14));p.setPen(isEnabled()?t->ink:t->muted);p.drawText(QRect(13,0,width()-45,height()),Qt::AlignVCenter,p.fontMetrics().elidedText(currentText(),Qt::ElideRight,width()-45));
        p.setPen(QPen(t->muted,1.6,Qt::SolidLine,Qt::RoundCap));double y=height()/2.;p.drawLine(QPointF(width()-23,y-2),QPointF(width()-19,y+2));p.drawLine(QPointF(width()-19,y+2),QPointF(width()-15,y-2));
    }
};
class StudyToggle : public QCheckBox {
public:
    Theme* t;StudyToggle(Theme* theme,QString label):QCheckBox(label),t(theme){setMinimumSize(132,40);}
    bool hitButton(const QPoint& p)const override{return rect().contains(p);}
    void paintEvent(QPaintEvent*)override{
        QPainter p(this);p.setRenderHints(QPainter::Antialiasing|QPainter::TextAntialiasing);double y=height()/2.;
        p.setPen(Qt::NoPen);p.setBrush(isChecked()?softMix(t->surface,t->accent,.8):t->line);p.drawRoundedRect(QRectF(1,y-10,33,20),10,10);p.setBrush(t->surface);p.drawEllipse(QPointF(isChecked()?25:10,y),7,7);
        ::text(p,{43,0,double(width()-43),double(height())},QCheckBox::text(),t->ink,13);
        if(hasFocus()){p.setPen(QPen(t->accent,1,Qt::DashLine));p.setBrush(Qt::NoBrush);p.drawRoundedRect(rect().adjusted(0,0,-1,-1),6,6);}
    }
};

class StudioStudyPanel : public QWidget {
public:
    Theme* t;bool active=false;
    explicit StudioStudyPanel(Theme* theme):t(theme){}
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);p.setRenderHint(QPainter::Antialiasing);
        QRectF r=QRectF(rect()).adjusted(.5,.5,-.5,-.5);
        p.setBrush(t->surface);p.setPen(QPen(active?softMix(t->line,t->accent,.45):t->line,1));p.drawRoundedRect(r,8,8);
        p.setPen(softMix(t->surface,t->ink,t->light?.08:.11));p.drawLine(QPointF(9,1),QPointF(width()-9,1));
    }
};

class StudyScrim : public QWidget {
public:
    Theme* t;explicit StudyScrim(Theme* theme,QWidget* parent):QWidget(parent),t(theme){}
    void paintEvent(QPaintEvent*) override {QPainter p(this);p.fillRect(rect(),QColor(0,0,0,t->soft?34:98));}
};

class GraphicStudyPlot : public QWidget {
public:
    Theme* t;QVector<QPointF> points;int pointIndex=1;bool dragging=false;
    std::function<void(int)> selectionChanged;std::function<void(QVector<QPointF>)> pointsChanged;
    GraphicStudyPlot(Theme* theme,QVector<QPointF> data):t(theme),points(data){setMinimumSize(340,170);setFocusPolicy(Qt::StrongFocus);setAccessibleName("그래픽 EQ의 예제 주파수와 게인. 방향키로 지점을 선택하고 값을 조절합니다.");}
    QRectF plot()const{return QRectF(46,16,width()-98,height()-50);}
    QPointF screen(QPointF value)const{QRectF r=plot();return {r.left()+std::log10(value.x()/20)/3*r.width(),r.top()+(12-value.y())/24*r.height()};}
    void paintEvent(QPaintEvent*)override{
        QPainter p(this);p.setRenderHints(QPainter::Antialiasing|QPainter::TextAntialiasing);p.fillRect(rect(),t->soft?t->surface:t->well);
        const QRectF area=plot();
        text(p,{area.left(),0,40,14},"dB",t->muted,11);text(p,{double(width()-24),double(height()-28),24,20},"Hz",t->muted,11);
        for(double g:{-12.,-6.,0.,6.,12.}){double y=screen({20,g}).y();p.setPen(QPen(g==0?t->muted:t->line,.8));p.drawLine(QPointF(area.left(),y),QPointF(area.right(),y));text(p,{0,y-9,36,18},QString::number(g),t->muted,12,false,Qt::AlignRight|Qt::AlignVCenter,!t->soft);}
        for(double f:{20.,100.,1000.,10000.,20000.}){double x=screen({f,0}).x();p.setPen(QPen(t->line,.7));p.drawLine(QPointF(x,area.top()),QPointF(x,area.bottom()));text(p,{x-25,area.bottom()+6,50,20},f>=1000?QString::number(f/1000)+"k":QString::number(f),t->muted,12,false,Qt::AlignCenter,!t->soft);}
        QPainterPath path;
        for(int i=0;i<points.size();++i){QPointF pos=screen(points[i]);if(!i)path.moveTo(pos);else path.lineTo(pos);}
        p.setPen(QPen(t->accent,t->soft?3:2));p.setBrush(Qt::NoBrush);p.drawPath(path);
        for(int i=0;i<points.size();++i){p.setPen(QPen(t->accent,2));p.setBrush(i==pointIndex?t->accent:t->surface);p.drawEllipse(screen(points[i]),i==pointIndex?6:4,i==pointIndex?6:4);}
        if(hasFocus()){p.setBrush(Qt::NoBrush);p.setPen(QPen(t->accent,1,Qt::DashLine));p.drawRoundedRect(rect().adjusted(2,2,-2,-2),6,6);}
    }
    void mousePressEvent(QMouseEvent* e)override{
        if(e->button()!=Qt::LeftButton || points.isEmpty())return;
        int best=0;double distance=1e9;for(int i=0;i<points.size();++i){double d=QLineF(e->position(),screen(points[i])).length();if(d<distance){best=i;distance=d;}}
        pointIndex=best;dragging=true;setFocus();if(selectionChanged)selectionChanged(best);update();
    }
    void mouseMoveEvent(QMouseEvent* e)override{
        if(!dragging || points.isEmpty())return;points[pointIndex].setY(qBound(-12.,12.-24*(e->position().y()-plot().top())/plot().height(),12.));if(pointsChanged)pointsChanged(points);update();
    }
    void mouseReleaseEvent(QMouseEvent*)override{dragging=false;}
    void keyPressEvent(QKeyEvent* e)override{
        if(points.isEmpty())return;
        if(e->key()==Qt::Key_Left || e->key()==Qt::Key_Right){pointIndex=qBound(0,pointIndex+(e->key()==Qt::Key_Left?-1:1),int(points.size()-1));if(selectionChanged)selectionChanged(pointIndex);update();return;}
        if(e->key()==Qt::Key_Up || e->key()==Qt::Key_Down){points[pointIndex].setY(qBound(-12.,points[pointIndex].y()+(e->key()==Qt::Key_Up?.1:-.1),12.));if(pointsChanged)pointsChanged(points);update();return;}QWidget::keyPressEvent(e);
    }
};

class RoutingStudyView : public QWidget {
public:
    Theme* t;double left=.5,right=.5;
    explicit RoutingStudyView(Theme* theme):t(theme){setMinimumHeight(theme->soft?126:178);setMinimumWidth(470);setAccessibleName("L 출력은 L과 R의 가중합, R 출력은 그대로 통과");}
    void paintEvent(QPaintEvent*)override{
        QPainter p(this);p.setRenderHints(QPainter::Antialiasing|QPainter::TextAntialiasing);
        if(t->soft){
            auto chip=[&](QRectF r,QString s,bool strong=false){p.setBrush(strong?t->selected:t->well);p.setPen(Qt::NoPen);p.drawRoundedRect(r,18,18);text(p,r,s,strong?t->accent:t->ink,16,strong,Qt::AlignCenter);};
            for(int row=0;row<2;++row){QRectF r(0,row*62,width()-2,54);p.setBrush(t->well);p.setPen(Qt::NoPen);p.drawRoundedRect(r,14,14);}
            chip({14,8,62,38},"L",true);text(p,{88,8,20,38},"=",t->muted,19,false,Qt::AlignCenter);
            chip({120,8,150,38},number(left,2)+" × L");text(p,{280,8,20,38},"+",t->muted,19,false,Qt::AlignCenter);chip({314,8,150,38},number(right,2)+" × R");
            chip({14,70,62,38},"R");text(p,{120,70,double(width()-140),38},"R 입력을 그대로 통과",t->muted,15);
        }else{
            p.fillRect(rect(),t->well);const double rx=width()-132;
            text(p,{18,0,150,22},"입력",t->muted,12);text(p,{rx,0,112,22},"출력",t->muted,12);
            auto port=[&](QRectF r,QString s){p.setBrush(t->surface);p.setPen(t->line);p.drawRoundedRect(r,6,6);text(p,r,s,t->ink,17,true,Qt::AlignCenter,true);};
            port({18,34,90,38},"L");port({18,116,90,38},"R");port({rx,34,112,38},"L");port({rx,116,112,38},"R");
            auto wire=[&](double y1,double y2,QColor color,double width){QPainterPath path;path.moveTo(108,y1);path.cubicTo(190,y1,rx-90,y2,rx,y2);p.setPen(QPen(color,width));p.setBrush(Qt::NoBrush);p.drawPath(path);};
            wire(53,53,t->accent,2);wire(135,53,t->accent,2);wire(135,135,t->muted,1);
            text(p,{width()*.42,24,92,26},number(left,2),t->accent,14,false,Qt::AlignCenter,true);
            text(p,{width()*.52,82,92,26},number(right,2),t->accent,14,false,Qt::AlignCenter,true);
            text(p,{width()*.52,136,120,26},"PASS",t->muted,11,false,Qt::AlignCenter,true);
        }
    }
};

class StudyPickerDelegate : public QStyledItemDelegate {
public:
    Theme* t;explicit StudyPickerDelegate(Theme* theme,QObject* parent):QStyledItemDelegate(parent),t(theme){}
    QSize sizeHint(const QStyleOptionViewItem&,const QModelIndex&)const override{return QSize(260,t->soft?64:44);}
    void paint(QPainter* p,const QStyleOptionViewItem& option,const QModelIndex& index)const override{
        p->save();p->setRenderHints(QPainter::Antialiasing|QPainter::TextAntialiasing);
        const QRectF r=option.rect.adjusted(3,2,-3,-2);const bool active=option.state & QStyle::State_Selected;
        p->setPen(Qt::NoPen);p->setBrush(active?t->selected:t->surface);p->drawRoundedRect(r,t->soft?18:5,t->soft?18:5);
        const QIcon icon=qvariant_cast<QIcon>(index.data(Qt::DecorationRole));icon.paint(p,QRect(int(r.left()+13),int(r.center().y()-12),24,24));
        text(*p,{r.left()+51,r.top()+(t->soft?8:0),r.width()-60,26},index.data().toString(),t->ink,15,true);
        if(t->soft)text(*p,{r.left()+51,r.top()+32,r.width()-60,23},index.data(Qt::UserRole+1).toString(),t->muted,12);
        p->restore();
    }
};
