#include "common.h"
#include <QDebug>
#include <QIcon>
#include <QHBoxLayout>
#include <QGraphicsBlurEffect>
#include <QMouseEvent>

#include "Shell.h"
#include "MainWindow.h"
#include "FilterMouseMove.h"
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <QX11Info>

#define _NET_WM_MOVERESIZE_SIZE_TOPLEFT      0
#define _NET_WM_MOVERESIZE_SIZE_TOP          1
#define _NET_WM_MOVERESIZE_SIZE_TOPRIGHT     2
#define _NET_WM_MOVERESIZE_SIZE_RIGHT        3
#define _NET_WM_MOVERESIZE_SIZE_BOTTOMRIGHT  4
#define _NET_WM_MOVERESIZE_SIZE_BOTTOM       5
#define _NET_WM_MOVERESIZE_SIZE_BOTTOMLEFT   6
#define _NET_WM_MOVERESIZE_SIZE_LEFT         7
#define _NET_WM_MOVERESIZE_MOVE              8


int bound(int min, int between, int max) {
    if (between < min) {
        return min;
    }
    if (between > max) {
        return max;
    }
    return between;
}

MainWindow::MainWindow(QWidget *parent) : QWidget(parent) {
    qDebug() << "Build with" << WebWidgetName;
    this->setWindowIcon(QIcon::fromTheme("deepin-appstore"));
    this->resizeContent(1028, 700);
    this->setMinimumContentSize(906, 680);
    this->setMouseTracking(true);
    this->setAttribute(Qt::WA_QuitOnClose, true);
    this->setAttribute(Qt::WA_DeleteOnClose, true);
    this->setAttribute(Qt::WA_TranslucentBackground, true);
    this->setWindowFlags(Qt::FramelessWindowHint);

    this->webView = new WebView(this);
    this->webView->setObjectName("webView");

    // Leave event will cause problems with <horizontal-resizer>, eat leave events!
    const auto filter = new FilterMouseMove(this);
    this->webView->installEventFilter(filter);

    this->horizontalLayout = new QHBoxLayout(this);
    this->horizontalLayout->setSpacing(0);
    this->horizontalLayout->setObjectName("horizontalLayout");
    this->horizontalLayout->setContentsMargins(this->resizeHandleWidth,
                                               this->resizeHandleWidth,
                                               this->resizeHandleWidth,
                                               this->resizeHandleWidth);
    this->horizontalLayout->addWidget(this->webView);
}

void MainWindow::polish() {
    // window shadow
    if (!this->shadowEffect) {
        this->shadowEffect = new QGraphicsDropShadowEffect(this);
        this->shadowEffect->setBlurRadius(resizeHandleWidth);
        this->shadowEffect->setColor(Qt::darkGray);
        this->shadowEffect->setOffset(0, 0);
        this->setGraphicsEffect(this->shadowEffect);
    }
}

void MainWindow::mousePressEvent(QMouseEvent* event) {
    int x = event->x();
    int y = event->y();
    CornerEdge ce = getCornerEdge(x, y);
    if (ce) {
        resizingCornerEdge = ce;
        beforeResizing = geometry();
        beforeResizingGlobalPos = event->globalPos();
        qDebug() << "mouse resize starts";
    }
}

void MainWindow::mouseMoveEvent(QMouseEvent* event) {
    int x = event->x();
    int y = event->y();

    CornerEdge ce = getCornerEdge(x, y);
    if (!resizingCornerEdge) {
        this->updateCursor(ce);
    }
    if (resizingCornerEdge && !recalcResizingInProgress) {
        recalcResizingInProgress = true;
        QRect newRect = beforeResizing;
        int deltaX = event->globalX() - beforeResizingGlobalPos.x();
        if (resizingCornerEdge & CornerEdge::Left) {
            int newWidth = bound(minimumWidth(),
                                 beforeResizing.width() + beforeResizingGlobalPos.x() - event->globalX(),
                                 maximumWidth());
            newRect.setX(beforeResizingGlobalPos.x() + beforeResizing.width() - newWidth);
            newRect.setWidth(newWidth);
        } else if (resizingCornerEdge & CornerEdge::Right) {
            newRect.setWidth(bound(minimumWidth(), beforeResizing.width() + deltaX, maximumWidth()));
        }

        int deltaY = event->globalY() - beforeResizingGlobalPos.y();
        if (resizingCornerEdge & CornerEdge::Top) {
            int newHeight = bound(minimumHeight(),
                                  beforeResizing.height() + beforeResizing.y() - event->globalY(),
                                  maximumHeight());
            newRect.setY(beforeResizingGlobalPos.y() + beforeResizing.height() - newHeight);
            newRect.setHeight(newHeight);
        } else if (resizingCornerEdge & CornerEdge::Bottom) {
            newRect.setHeight(bound(minimumHeight(), beforeResizing.height() + deltaY, maximumHeight()));
        }

        setGeometry(newRect);
        recalcResizingInProgress = false;
    }

}

void MainWindow::mouseReleaseEvent(QMouseEvent* UNUSED(event)) {
    if (resizingCornerEdge) {
        resizingCornerEdge = CornerEdge::Nil;
        recalcResizingInProgress = false;
        this->updateCursor(resizingCornerEdge);
        qDebug() << "mouse resize ends";
    }
}

MainWindow::~MainWindow() {

}

CornerEdge MainWindow::getCornerEdge(int x, int y) {
    QSize winSize = size();
    unsigned int ce = (unsigned int)CornerEdge::Nil;
    if (y <= resizeHandleWidth) {
        ce = ce | (unsigned int)CornerEdge::Top;
    }
    if (x <= resizeHandleWidth) {
        ce = ce | (unsigned int)CornerEdge::Left;
    }
    if (winSize.height() - resizeHandleWidth <= y) {
        ce = ce | (unsigned int)CornerEdge::Bottom;
    }
    if (winSize.width() - resizeHandleWidth <= x) {
        ce = ce | (unsigned int)CornerEdge::Right;
    }
    return (CornerEdge)ce;
}

void MainWindow::updateCursor(CornerEdge ce) {
    switch (ce) {
        case CornerEdge::Nil: {
            this->unsetCursor();
            break;
        }
        case CornerEdge::Top:
        case CornerEdge::Bottom: {
            this->setCursor(Qt::SizeVerCursor);
            break;
        }
        case CornerEdge::Left:
        case CornerEdge::Right: {
            this->setCursor(Qt::SizeHorCursor);
            break;
        }
        case CornerEdge::TopLeft:
        case CornerEdge::BottomRight: {
            this->setCursor(Qt::SizeFDiagCursor);
            break;
        }
        case CornerEdge::TopRight:
        case CornerEdge::BottomLeft: {
            this->setCursor(Qt::SizeBDiagCursor);
            break;
        };
    }
}

void MainWindow::startMoving(int x, int y) {
    XEvent xev;
    Atom netMoveResize = XInternAtom(QX11Info::display(), "_NET_WM_MOVERESIZE", false);
    xev.xclient.type = ClientMessage;
    xev.xclient.message_type = netMoveResize;
    xev.xclient.display = QX11Info::display();

    xev.xclient.window = this->winId();
    xev.xclient.format = 32;
    xev.xclient.data.l[0] = x;
    xev.xclient.data.l[1] = y;
    xev.xclient.data.l[2] = _NET_WM_MOVERESIZE_MOVE;
    xev.xclient.data.l[3] = Button1;
    xev.xclient.data.l[4] = 0;
    XUngrabPointer(QX11Info::display(), QX11Info::appTime());

    XSendEvent(QX11Info::display(),
               QX11Info::appRootWindow(QX11Info::appScreen()),
               false,
               SubstructureRedirectMask | SubstructureNotifyMask,
               &xev);

//    // release the mouse
//    QPointF releasePoint;
//    QMouseEvent* mouseUpSimulation = new QMouseEvent(QEvent::MouseButtonRelease,
//                                                     releasePoint,
//                                                     Qt::LeftButton,
//                                                     Qt::LeftButton,
//                                                     Qt::NoModifier);
//    qApp->sendEvent(webView, mouseUpSimulation);
}

void MainWindow::toggleMaximized() {
    if (this->isMaximized()) {
        this->showNormal();
    } else {
        this->showMaximized();
    }
}

void MainWindow::resizeContent(int w, int h) {
    this->resize(w + resizeHandleWidth * 2,
                 h + resizeHandleWidth * 2);
}

void MainWindow::setMinimumContentSize(int w, int h) {
    this->setMinimumSize(w + resizeHandleWidth * 2,
                         h + resizeHandleWidth * 2);
}

void MainWindow::changeEvent(QEvent *event) {
    if (event->type() == QEvent::WindowStateChange) {
        if (this->windowState() & Qt::WindowMaximized) {
            this->horizontalLayout->setContentsMargins(0, 0, 0, 0);
        } else {
            this->horizontalLayout->setContentsMargins(resizeHandleWidth,
                                                       resizeHandleWidth,
                                                       resizeHandleWidth,
                                                       resizeHandleWidth);
        }
        emit this->windowStateChanged((Qt::WindowState)(int)this->windowState());
    }
}

void MainWindow::setUrl(const QUrl &url) {
    this->webView->setUrl(url);
}

void MainWindow::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_F1 &&
        event->modifiers() == Qt::NoModifier) {
        const auto shell = static_cast<Shell*>(qApp);
        shell->openManual();
    };
}
