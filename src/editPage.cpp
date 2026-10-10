/*
* Copyright (C) 2020 Tianjin KYLIN Information Technology Co., Ltd.
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 3, or (at your option)
* any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program; if not, see <http://www.gnu.org/licenses/&gt;.
*
*/

#include <QBitmap>
#include <QPalette>
#include <QPainter>
#include <QTextList>
//#include <QClipboard>
#include <QDebug>
#include <QFileDialog>
#include <QImageReader>
#include <QResizeEvent>
#include <QTimer>
#include <QScreen>
#include <QWindow>

#include <KWindowSystem>
// Necessary for closeApplication()
#include <NETWM>

#include "widget.h"
#include "ui_widget.h"
#include "editPage.h"
#include "ui_editPage.h"
#include "windowmanage.hpp"

extern void qt_blurImage(QImage &blurImage, qreal radius, bool quality, int transposed);

EditPage::EditPage(Widget *page, int noteId, QWidget *parent) :
    QWidget(parent),
    ui(new Ui::Edit_page),
    m_editColor(0, 0, 0),
    m_noteId(noteId),
    m_lastBlockList(0),
    m_mousePressed(false),
    m_defaultFontColorChanged(false),
    m_notebook(page),
    m_colorPage(new SelectColor(m_notebook, this)),
    m_setSizePage(new SetFontSize(m_notebook)),
    m_setColorFontPage(new SetFontColor(m_notebook)),
    m_isFullscreen(false),
    m_isInsImg(false)
{
    ui->setupUi(this);
    initSetup();
    listenToGsettings();
    slotsSetup();
    installEventFilter(this);
}

EditPage::~EditPage()
{
    delete ui;
}

void EditPage::showWithRememberedSize()
{
    if (isVisible()) {
        QWidget::show();
        return;
    }

    m_restoringSize = true;
    const quint64 generation = ++m_restoreGeneration;
    const Qt::WindowStates previousState = windowState();
    const bool wasSpecialState = previousState & (Qt::WindowMinimized | Qt::WindowMaximized | Qt::WindowFullScreen);
    m_reopenNormalState = wasSpecialState;
    if (!m_lastNormalSize.isValid() || !wasSpecialState)
        m_lastNormalSize = size();
    if (wasSpecialState) {
        setWindowState(Qt::WindowNoState);
        m_isFullscreen = false;
    }
    QSize restoredSize = m_notebook ? m_notebook->preferredEditorSize() : QSize(300, 280);
    QScreen *screen = windowHandle() ? windowHandle()->screen() : QGuiApplication::primaryScreen();
    if (screen) {
        restoredSize = restoredSize.boundedTo(screen->availableGeometry().size()
                                              .expandedTo(minimumSize()));
    }
    m_restoredSize = restoredSize.expandedTo(minimumSize());
    // Send the resize after the map request. KWin can retain the previous
    // native geometry when a hidden editor object is reused.
    QWidget::show();
    if (wasSpecialState) {
        // Hidden QWindow state changes can be superseded by retained X11
        // state on remapping. Clear it after the map request as well.
        KWindowSystem::clearState(winId(), NET::Max | NET::FullScreen);
    }
    resize(m_restoredSize);
    m_restoringSize = false;
    QTimer::singleShot(300, this, [this, generation]() {
        // A successful map may not emit a resize event when the size already
        // matches. Expire the acknowledgement guard after mapping settles.
        if (generation != m_restoreGeneration)
            return;
        if (!(windowState() & (Qt::WindowMinimized | Qt::WindowMaximized | Qt::WindowFullScreen))) {
            m_reopenNormalState = false;
            if (m_restoredSize.isValid() && size() == m_restoredSize) {
                m_lastNormalSize = size();
                m_restoredSize = QSize();
            }
        }
    });
}

void EditPage::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    if (event->type() != QEvent::WindowStateChange)
        return;
    if (!event->spontaneous() && isVisible() && !m_restoringSize) {
        // An explicit state change on an open editor belongs to the user.
        m_reopenNormalState = false;
    } else if (event->spontaneous() && m_reopenNormalState && isVisible()
               && (windowState() & (Qt::WindowMinimized | Qt::WindowMaximized | Qt::WindowFullScreen))) {
        const quint64 generation = m_restoreGeneration;
        QTimer::singleShot(0, this, [this, generation]() {
            if (generation != m_restoreGeneration || !m_reopenNormalState || !isVisible())
                return;
            m_reopenNormalState = false;
            m_restoringSize = true;
            showNormal();
            KWindowSystem::clearState(winId(), NET::Max | NET::FullScreen);
            resize(m_restoredSize.isValid() ? m_restoredSize : m_lastNormalSize);
            m_restoringSize = false;
        });
    }
}

void EditPage::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    if (m_restoringSize || !isVisible()
            || (windowState() & (Qt::WindowMinimized | Qt::WindowMaximized | Qt::WindowFullScreen)))
        return;

    // Ignore the old geometry and the requested geometry reported by X11
    // during remapping. A different size is a new normal resize.
    if (m_restoredSize.isValid() && event->spontaneous()) {
        if (event->size() == m_lastNormalSize)
            return;
        if (event->size() == m_restoredSize) {
            m_lastNormalSize = event->size();
            m_restoredSize = QSize();
            return;
        }
    }
    m_restoredSize = QSize();

    // Returning from a window state change restores the last normal geometry;
    // it must not overwrite a newer size chosen in another editor.
    if (event->size() != m_lastNormalSize) {
        m_lastNormalSize = event->size();
        if (m_notebook)
            m_notebook->rememberEditorSize(event->size());
    }
}

void EditPage::showAnchoredPopup(QWidget *popup, QWidget *button, bool preferAbove)
{
    const QRect anchor(button->mapToGlobal(QPoint(0, 0)), button->size());
    QScreen *screen = QGuiApplication::screenAt(anchor.center());
    if (!screen)
        screen = windowHandle() ? windowHandle()->screen() : QGuiApplication::primaryScreen();

    const int gap = 4;
    const int above = anchor.top() - popup->height() - gap;
    const int below = anchor.bottom() + 1 + gap;
    QPoint position(anchor.left(), preferAbove ? above : below);
    if (screen) {
        const QRect available = screen->availableGeometry();
        const bool fitsAbove = above >= available.top();
        const bool fitsBelow = below + popup->height() <= available.bottom() + 1;
        if (preferAbove && !fitsAbove && fitsBelow)
            position.setY(below);
        else if (!preferAbove && !fitsBelow && fitsAbove)
            position.setY(above);
        position.setX(qBound(available.left(), position.x(),
                            qMax(available.left(), available.right() + 1 - popup->width())));
        position.setY(qBound(available.top(), position.y(),
                            qMax(available.top(), available.bottom() + 1 - popup->height())));
    }
    popup->move(position);
    popup->show();
}

void EditPage::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);  // 反锯齿;
    QPainterPath rectPath;
    rectPath.addRect(this->rect());
    p.fillPath(rectPath, palette().color(QPalette::Base));
}

bool EditPage::eventFilter(QObject *obj,QEvent *event)
{
    if (obj == this) {
        if (event->type() == QEvent::WindowActivate) {
            m_noteHead->hide();
            m_noteHeadMenu->show();
        }
        if (event->type() == QEvent::WindowDeactivate) {
            m_noteHead->show();
            m_noteHeadMenu->hide();
        }
    }
    return false;
}

void EditPage::initSetup()
{
    // 标题
    this->setWindowTitle(tr("Notes"));


#if (QT_VERSION >= QT_VERSION_CHECK(5, 12, 0))
    // 高分屏适配
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
#endif

    kabase::WindowManage::removeHeader(this);
    // 配置按钮
    btnSetup();
    initColor();
    // 获取字体按钮状态
    m_fontLabel = new QLabel(this);
    m_fontBtnLayout = new QVBoxLayout(this);
    m_fontBtnLayout->addWidget(m_fontLabel);
    ui->fontSizeBtn->setLayout(m_fontBtnLayout);
    QFont dateFont;
    dateFont.setPointSize(11);
    m_fontLabel->setFont(dateFont);
    fontChanged(ui->textEdit->font());

    m_noteHead = new noteHead(this);
    m_noteHeadMenu = new noteHeadMenu(this);

    ui->verticalLayout_3->addWidget(m_noteHead);
    ui->verticalLayout_3->addWidget(m_noteHeadMenu);
    m_noteHeadMenu->hide();

    ui->textEdit->setFrameShape(QFrame::NoFrame);
    ui->textEdit->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    // 右键菜单
    ui->textEdit->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(ui->textEdit, SIGNAL(customContextMenuRequested(QPoint)), this, SLOT(textRightMenu(QPoint)));

    // 设置字体大小
    m_setSizePage = new SetFontSize(this);
    m_setSizePage->hide();
    // 设置字体颜色
    m_setColorFontPage = new SetFontColor(this);
    m_setColorFontPage->hide();
}

void EditPage::btnSetup()
{
    ui->fontColorBtn->setAttribute(Qt::WA_TransparentForMouseEvents);
    ui->fontColorBtn->setFocusPolicy(Qt::NoFocus);
    ui->unorderedBtn->setIcon(QIcon::fromTheme("view-list-symbolic"));
    ui->strikeOutBtn->setIcon(QIcon::fromTheme("format-text-strikethrough-symbolic"));
    ui->orderedBtn->setIcon(QIcon::fromTheme("ukui-view-list-numbe-symbolic"));
    ui->insertBtn->setIcon(QIcon::fromTheme("folder-pictures-symbolic"));
    ui->underlineBtn->setIcon(QIcon::fromTheme("format-text-underline-symbolic"));
    ui->boldBtn->setIcon(QIcon::fromTheme("format-text-bold-symbolic"));
    ui->italicBtn->setIcon(QIcon::fromTheme("format-text-italic-symbolic"));

    ui->unorderedBtn->setIconSize(QSize(20, 24));
    ui->strikeOutBtn->setIconSize(QSize(20, 17));
    ui->orderedBtn->setIconSize(QSize(20, 14));
    ui->insertBtn->setIconSize(QSize(20, 24));
    ui->underlineBtn->setIconSize(QSize(20, 16));
    ui->boldBtn->setIconSize(QSize(20, 17));
    ui->italicBtn->setIconSize(QSize(20, 17));

    ui->boldBtn->setToolTip(tr("Bold"));
    ui->italicBtn->setToolTip(tr("Italic"));
    ui->underlineBtn->setToolTip(tr("Underline"));
    ui->strikeOutBtn->setToolTip(tr("Strikeout"));
    ui->unorderedBtn->setToolTip(tr("Unordered"));
    ui->orderedBtn->setToolTip(tr("Ordered"));

    ui->fontSizeBtn->setToolTip(tr("Font Size"));
    ui->styleBtn->setToolTip(tr("Font Color"));
    ui->insertBtn->setToolTip(tr("InsertPicture"));


    ui->boldBtn->setProperty("isWindowButton", 0x1);
    ui->boldBtn->setProperty("useIconHighlightEffect", 0x2);
    ui->boldBtn->setFlat(true);

    ui->italicBtn->setProperty("isWindowButton", 0x1);
    ui->italicBtn->setProperty("useIconHighlightEffect", 0x2);
    ui->italicBtn->setFlat(true);

    ui->underlineBtn->setProperty("isWindowButton", 0x1);
    ui->underlineBtn->setProperty("useIconHighlightEffect", 0x2);
    ui->underlineBtn->setFlat(true);

    ui->strikeOutBtn->setProperty("isWindowButton", 0x1);
    ui->strikeOutBtn->setProperty("useIconHighlightEffect", 0x2);
    ui->strikeOutBtn->setFlat(true);

    ui->unorderedBtn->setProperty("isWindowButton", 0x1);
    ui->unorderedBtn->setProperty("useIconHighlightEffect", 0x2);
    ui->unorderedBtn->setFlat(true);

    ui->orderedBtn->setProperty("isWindowButton", 0x1);
    ui->orderedBtn->setProperty("useIconHighlightEffect", 0x2);
    ui->orderedBtn->setFlat(true);

    ui->insertBtn->setProperty("isWindowButton", 0x1);
    ui->insertBtn->setProperty("useIconHighlightEffect", 0x2);
    ui->insertBtn->setFlat(true);

}

void EditPage::slotsSetup()
{
    connect(m_noteHeadMenu->ui->pushButtonExit, &QPushButton::clicked, this, [=](){
        if (ui->textEdit->document()->isEmpty()) {
            emit isEmptyNote(m_noteId);
        }
        this->close();
    });
    connect(m_noteHeadMenu->m_menuActionDel, &QAction::triggered, this, [=](){
        this->close();
        requestDel(m_noteId);
    });
    connect(ui->textEdit, &QTextEdit::textChanged, this, &EditPage::textChangedSlot);
    // 调色板菜单
    connect(m_noteHeadMenu->ui->pushButtonPalette, &QPushButton::clicked, this, [=](){
        showAnchoredPopup(m_colorPage, m_noteHeadMenu->ui->pushButtonPalette, false);
    });
    // 调色按钮
    connect(m_colorPage->paletteWidget->ui->blueBtn, &QPushButton::clicked, this,
            &EditPage::blueBtnSlot);
    connect(m_colorPage->paletteWidget->ui->redBtn, &QPushButton::clicked, this,
            &EditPage::redBtnSlot);
    connect(m_colorPage->paletteWidget->ui->darkGreenBtn, &QPushButton::clicked, this,
            &EditPage::darkGreenBtnSlot);
    connect(m_colorPage->paletteWidget->ui->orangeBtn, &QPushButton::clicked, this,
            &EditPage::orangeBtnSlot);
    connect(m_colorPage->paletteWidget->ui->purpleBtn, &QPushButton::clicked, this,
            &EditPage::purpleBtnSlot);
    connect(m_colorPage->paletteWidget->ui->goldenBtn, &QPushButton::clicked, this,
            &EditPage::goldenBtnSlot);
    connect(m_colorPage->paletteWidget->ui->greyBtn, &QPushButton::clicked, this,
            &EditPage::greyBtnSlot);
    connect(m_colorPage->paletteWidget->ui->pinkBtn, &QPushButton::clicked, this,
            &EditPage::pinkBtnSlot);
    // 字体颜色大小
    connect(m_setSizePage->ui->listWidget, &QListWidget::itemClicked, this,
            &EditPage::setFontSizeSlot);
    connect(m_setColorFontPage->ui->listWidget, &QListWidget::itemClicked, this,
            &EditPage::setFontColorSlot);
    // 文本格式
    connect(ui->boldBtn, &QPushButton::clicked, this, &EditPage::setBoldSlot);
    connect(ui->italicBtn, &QPushButton::clicked, this, &EditPage::setItalicSlot);
    connect(ui->underlineBtn, &QPushButton::clicked, this, &EditPage::setUnderlineSlot);
    connect(ui->strikeOutBtn, &QPushButton::clicked, this, &EditPage::setStrikeOutSlot);
    connect(ui->unorderedBtn, &QPushButton::clicked, this, &EditPage::setUnorderedListSlot);
    connect(ui->orderedBtn, &QPushButton::clicked, this, &EditPage::setOrderedListSlot);
    connect(ui->insertBtn, &QPushButton::clicked, this, &EditPage::insertpicture);
    connect(ui->textEdit, &QTextEdit::cursorPositionChanged, this,
            &EditPage::cursorPositionChangedSlot);
    connect(ui->textEdit, &QTextEdit::currentCharFormatChanged, this,
            &EditPage::currentCharFormatChangedSlot);

    connect(ui->fontSizeBtn, &QPushButton::clicked, this, [=](){
        showAnchoredPopup(m_setSizePage, ui->fontSizeBtn, true);
    });
    connect(ui->styleBtn, &QPushButton::clicked, this, &EditPage::onFontColorClicked);
    connect(m_noteHeadMenu, &noteHeadMenu::requestUpdateMenuIcon, this, [=](){
        KWindowInfo info(this->winId(), NET::WMState);
        bool b = info.state() & NET::KeepAbove;

        if(b){
            m_noteHeadMenu->m_topAction->setIcon(QPixmap(":/image/1x/select.png"));
        }
        else{
            m_noteHeadMenu->m_topAction->setIcon(QPixmap(""));
        }
    });


    connect(m_noteHeadMenu, &noteHeadMenu::requestUpdateMenuIcon, this, [=](){
        KWindowInfo info(this->winId(), NET::WMState);
        bool b = info.state() & NET::KeepAbove;

        if(b){
            m_noteHeadMenu->m_topAction->setIcon(QPixmap(":/image/1x/select.png"));
        }
        else{
            m_noteHeadMenu->m_topAction->setIcon(QPixmap(""));
        }
    });

    connect(m_noteHeadMenu, &noteHeadMenu::requestTopMost, this, [=](){
         KWindowInfo info(this->winId(), NET::WMState);
         bool b = info.state() & NET::KeepAbove;

         if(b) {
             setWindowStatusClear();
         }
         else {
             setWindowKeepAbove();
         }
     });
}

void EditPage::mergeFormatOnWordOrSelection(const QTextCharFormat &format)
{
    QTextCursor cursor = ui->textEdit->textCursor();
    if (!cursor.hasSelection()) {
        // cursor.select(QTextCursor::WordUnderCursor);
    }
    cursor.mergeCharFormat(format);
    ui->textEdit->mergeCurrentCharFormat(format);
    ui->textEdit->setFocus(Qt::TabFocusReason);
}

void EditPage::fontChanged(const QFont &f)
{
    qDebug() << "font Changed" << f.pointSize() << f.bold();
    m_setSizePage->ui->listWidget->setCurrentRow(f.pointSize() - 10);
    m_fontLabel->setText(QString::number(f.pointSize()));

    if(f.pointSize() < 10 )
    {
        m_fontLabel->setText(QString::number(10));
    }
    else
    {
        m_fontLabel->setText(QString::number(f.pointSize()));
    }
    ui->boldBtn->setChecked(f.bold());
    ui->italicBtn->setChecked(f.italic());
    ui->underlineBtn->setChecked(f.underline());
    ui->strikeOutBtn->setChecked(f.strikeOut());

    if (ui->textEdit->textCursor().currentList()) {
        QTextListFormat lfmt = ui->textEdit->textCursor().currentList()->format();
        if (lfmt.style() == QTextListFormat::ListDisc) {
            ui->unorderedBtn->setChecked(true);
            ui->orderedBtn->setChecked(false);
        } else if (lfmt.style() == QTextListFormat::ListDecimal) {
            ui->unorderedBtn->setChecked(false);
            ui->orderedBtn->setChecked(true);
        } else {
            ui->unorderedBtn->setChecked(false);
            ui->orderedBtn->setChecked(false);
        }
    } else {
        ui->unorderedBtn->setChecked(false);
        ui->orderedBtn->setChecked(false);
    }
}

void EditPage::fontColorChanged(const QColor &c)
{
    qDebug() << "fontColorChanged" << c.name();
    QString _Stylesheet;
    QString _BgColor;
    _BgColor = c.name();

    if (_BgColor == "#000000" || _BgColor == "#FFFFFF" || c == QColor()) {
        m_defaultFontColorChanged = false;
        if (_BgColor == "#000000" && QGSettings::isSchemaInstalled(THEME_QT_SCHEMA)) {
            QByteArray qtThemeID(THEME_QT_SCHEMA);
            auto qtThemeSetting = new QGSettings(qtThemeID,QByteArray(),this);
            QString style = qtThemeSetting->get("styleName").toString();
            if(style == "ukui-dark") {
                _BgColor = "#FFFFFF";
            }
            else if(style == "ukui-default" || style == "ukui-white" || style == "ukui-light"){
                _BgColor = "#000000";
            }
        }
    }
    _Stylesheet = "background-color: %1;";
    _Stylesheet = _Stylesheet.arg(_BgColor);

    ui->fontColorBtn->setStyleSheet(_Stylesheet +"border-radius:3px;");
}

void EditPage::list(bool checked, QTextListFormat::Style style)
{
    QTextCursor cursor = ui->textEdit->textCursor();
    cursor.beginEditBlock();
    if (!checked) {
        qDebug() << "unchecked";
        QTextBlockFormat obfmt = cursor.blockFormat();
        QTextBlockFormat bfmt;
        bfmt.setIndent(obfmt.indent());
        cursor.setBlockFormat(bfmt);
//        QTextDocument *document = ui->textEdit->document();
//        document->setIndentWidth(0);
    } else {
        qDebug() << "checked";
        QTextCharFormat charFormat = cursor.charFormat();
        qDebug() << charFormat.fontPointSize() << cursor.selectedText() ;
        qDebug() << cursor.anchor() << cursor.position();
        QTextListFormat listFmt;
        if (cursor.currentList()) {
            listFmt = cursor.currentList()->format();
        }
        listFmt.setStyle(style);
//        QTextDocument *document = ui->textEdit->document();
//        document->setIndentWidth(15);
        cursor.createList(listFmt);
    }
    cursor.endEditBlock();
}

/*!
 * \brief Edit_page::listenToGsettings
 *
 */
void EditPage::listenToGsettings()
{
    // 监听主题改变
    const QByteArray id(THEME_QT_SCHEMA);
    if (QGSettings::isSchemaInstalled(id)) {
        QGSettings *styleSettings = new QGSettings(id, QByteArray(), this);
        auto style = styleSettings->get("styleName").toString();
        //if (ui->textEdit->document()->isEmpty()) {
            if (style == "ukui-default" || style == "ukui-white"
                || style == "ukui-light" || style == "ukui") {
                ui->fontColorBtn->setStyleSheet("background-color: black;"
                                                "border-radius:3px;");
            } else if (style == "ukui-dark" || style == "ukui-black") {
                ui->fontColorBtn->setStyleSheet("background-color: white;"
                                                "border-radius:3px;");
            }
        //}

        connect(styleSettings, &QGSettings::changed, this, [=](const QString &key){
            if (/*ui->textEdit->document()->isEmpty() && */!m_defaultFontColorChanged) {
                if (key == "styleName") {
                    QString currentTheme = styleSettings->get(MODE_QT_KEY).toString();
                    if (currentTheme == "ukui-default" || currentTheme == "ukui-white"
                        || currentTheme == "ukui-light" || currentTheme == "ukui") {
                        ui->fontColorBtn->setStyleSheet("background-color: black;"
                                                        "border-radius:3px;");
                        ui->frame->setStyleSheet("background-color:rgb(217, 217, 217)");
                        ui->frame_2->setStyleSheet("background-color:rgb(217, 217, 217)");

                    } else if (currentTheme == "ukui-dark" || currentTheme == "ukui-black") {
                        ui->fontColorBtn->setStyleSheet("background-color: white;"
                                                        "border-radius:3px;");
                        ui->frame->setStyleSheet("background-color:rgb(77, 77, 77)");
                        ui->frame_2->setStyleSheet("background-color:rgb(77, 77, 77)");

                    }
                }
            }
        });
    }
}

/**************** Slots *******************/

void EditPage::cursorPositionChangedSlot()
{
    qDebug() << "cursorPositionChangedSlot";
    QTextList *l = ui->textEdit->textCursor().currentList();
    if (m_lastBlockList && (l == m_lastBlockList || (l != 0 && m_lastBlockList != 0
                                                     && l->format().style()
                                                     == m_lastBlockList->format().style()))) {
        return;
    }
    m_lastBlockList = l;
    if (l) {
        QTextListFormat lfmt = l->format();
        if (lfmt.style() == QTextListFormat::ListDisc) {
            ui->unorderedBtn->setChecked(true);
            ui->orderedBtn->setChecked(false);
        } else if (lfmt.style() == QTextListFormat::ListDecimal) {
            ui->unorderedBtn->setChecked(false);
            ui->orderedBtn->setChecked(true);
        } else {
            ui->unorderedBtn->setChecked(false);
            ui->orderedBtn->setChecked(false);
        }
    } else {
        ui->unorderedBtn->setChecked(false);
        ui->orderedBtn->setChecked(false);
    }
}

void EditPage::currentCharFormatChangedSlot(const QTextCharFormat &format)
{
    qDebug() << "currentCharFormatChangedSlot";
    fontChanged(format.font());
    // fontColorChanged((format.foreground().isOpaque()) ? format.foreground().color() : QColor());
    fontColorChanged(format.foreground().color());
}

void EditPage::textChangedSlot()
{
    qDebug() << "emit textchange" << "note id" << m_noteId << this->m_id;
    emit texthasChanged(m_noteId, this->m_id);
}

//textedit右键菜单
void EditPage::textRightMenu(QPoint)
{
    QMenu *m_rightMenu = new QMenu;

    //定义action
    QAction *undoact = new QAction(tr("undo"));
    QAction *redoact = new QAction(tr("redo"));
    QAction *cutact = new QAction(tr("cut"));
    QAction *copyact = new QAction(tr("copy"));
    QAction *pasteact = new QAction(tr("paste"));
    QAction *textfornew = new QAction(tr("copy to newpage"));

    undoact->setIcon(QIcon::fromTheme("edit-undo"));
    redoact->setIcon(QIcon::fromTheme("edit-redo"));
    cutact->setIcon(QIcon::fromTheme("edit-cut"));
    copyact->setIcon(QIcon::fromTheme("edit-copy"));
    pasteact->setIcon(QIcon::fromTheme("edit-paste"));
//    textfornew->setIcon(QIcon::fromTheme("add-files-to-archive"));
    textfornew->setIcon(QIcon(":/image/1x/copy_to_new_note.png"));
    //定义action end

    //action使能与非使能
    connect(ui->textEdit->document(), SIGNAL(undoAvailable(bool)),
            undoact, SLOT(setEnabled(bool)));
    connect(ui->textEdit->document(), SIGNAL(redoAvailable(bool)),
            redoact, SLOT(setEnabled(bool)));
    undoact->setEnabled(ui->textEdit->document()->isUndoAvailable());
    redoact->setEnabled(ui->textEdit->document()->isRedoAvailable());

    cutact->setEnabled(!ui->textEdit->textCursor().selectedText().isEmpty());
    copyact->setEnabled(!ui->textEdit->textCursor().selectedText().isEmpty());
    pasteact->setEnabled(!QApplication::clipboard()->text().isEmpty());
    connect(QApplication::clipboard(), &QClipboard::dataChanged, this, [=](){
        pasteact->setEnabled(!QApplication::clipboard()->text().isEmpty());
    });

    textfornew->setEnabled(!ui->textEdit->textCursor().selectedText().isEmpty());
    //action使能与非使能 end

    //action绑定槽函数
    connect(undoact, SIGNAL(triggered(bool)), ui->textEdit, SLOT(undo()));
    connect(redoact, SIGNAL(triggered(bool)), ui->textEdit, SLOT(redo()));

    connect(cutact, SIGNAL(triggered()), ui->textEdit, SLOT(cut()));
    connect(copyact, SIGNAL(triggered()), ui->textEdit, SLOT(copy()));
    connect(pasteact, SIGNAL(triggered(bool)), ui->textEdit, SLOT(paste()));

    connect(textfornew, SIGNAL(triggered()), this, SLOT(textForNewEditpageSlot()));
    //action绑定槽函数 end

    //action添加到menu
    m_rightMenu->addAction(undoact);
    m_rightMenu->addAction(redoact);
    m_rightMenu->addSeparator();         // 添加分割线

    m_rightMenu->addAction(cutact);
    m_rightMenu->addAction(copyact);
    m_rightMenu->addAction(pasteact);
    m_rightMenu->addSeparator();         // 添加分割线

    m_rightMenu->addAction(textfornew);
    //action添加到menu end

    m_rightMenu->move (cursor().pos());
    m_rightMenu->show();

}

//拷贝选中内容到新便签页
void EditPage::textForNewEditpageSlot()
{
    if(!ui->textEdit->textCursor().selectedText().isEmpty())
    {
        ui->textEdit->copy();
        emit textForNewEditpage();
    }
}

// 加粗
void EditPage::setBoldSlot()
{
    qDebug() << "setBoldSlot";
    QTextCharFormat fmt;
    fmt.setFontWeight(ui->boldBtn->isCheckable() ? QFont::Bold : QFont::Normal);

    QTextCursor cursor = ui->textEdit->textCursor();
    if (cursor.charFormat().fontWeight() == QFont::Bold) {
        qDebug() << "current cursor charFormat QFont::Bold";
        fmt.setFontWeight(QFont::Normal);
        ui->textEdit->mergeCurrentCharFormat(fmt);
    } else {
        qDebug() << "current cursor charFormat QFont::Normal";
        fmt.setFontWeight(QFont::Bold);
        ui->textEdit->mergeCurrentCharFormat(fmt);
    }
}

// 斜体
void EditPage::setItalicSlot()
{
    qDebug()<<"-------setItalicSlot------------";
    QTextCharFormat fmt;
    fmt.setFontItalic(ui->italicBtn->isCheckable());// ? QFont::StyleItalic : QFont::Normal);
    // mergeFormatOnWordOrSelection(fmt);

    QTextCursor cursor = ui->textEdit->textCursor();
    if (cursor.charFormat().fontItalic()) { // return boolProperty(FontItalic)
        fmt.setFontItalic(QFont::StyleNormal);
        ui->textEdit->mergeCurrentCharFormat(fmt);
    } else {
        fmt.setFontItalic(QFont::StyleItalic);
        ui->textEdit->mergeCurrentCharFormat(fmt);
    }
}

// 划线
void EditPage::setUnderlineSlot()
{
    qDebug() << "setUnderlineSlot";
    QTextCharFormat fmt;
    fmt.setFontUnderline(ui->underlineBtn->isCheckable());// ? QFont::UnderlineResolved : QFont::Normal );
    // mergeFormatOnWordOrSelection(fmt);

    QTextCursor cursor = ui->textEdit->textCursor();
    if (cursor.charFormat().fontUnderline()) { //
        fmt.setFontUnderline(QFont::StyleNormal);
        ui->textEdit->mergeCurrentCharFormat(fmt);
    } else {
        ui->textEdit->mergeCurrentCharFormat(fmt);
    }
}

// 除线
void EditPage::setStrikeOutSlot()
{
    qDebug() << "setStrikeOutSlot";
    QTextCharFormat fmt;
    fmt.setFontStrikeOut(ui->strikeOutBtn->isCheckable());// ? QFont::StrikeOutResolved : QFont::Normal );
    // mergeFormatOnWordOrSelection(fmt);

    QTextCursor cursor = ui->textEdit->textCursor();
    if (cursor.charFormat().fontStrikeOut()) {
        fmt.setFontStrikeOut(QFont::StyleNormal);
        ui->textEdit->mergeCurrentCharFormat(fmt);
    } else {
        ui->textEdit->mergeCurrentCharFormat(fmt);
    }
}

// 无序列表
void EditPage::setUnorderedListSlot(bool checked)
{
    qDebug() << "setUnorderedListSlot" << checked;
    if (checked) {
        ui->orderedBtn->setChecked(false);
        qDebug() << "show list set false" << checked;
        qDebug() << "text_edit_page->ui->orderedBtn is checked ? :" << ui->orderedBtn->isChecked();
    }
    list(checked, QTextListFormat::ListDisc);
    qDebug() << "text_edit_page->ui->orderedBtn is checked ? :" << ui->orderedBtn->isChecked();
}

// 序列表
void EditPage::setOrderedListSlot(bool checked)
{
    qDebug() << "show num list";
    if (checked) {
        ui->unorderedBtn->setChecked(false);
    }
    list(checked, QTextListFormat::ListDecimal);
}

// 字号
void EditPage::setFontSizeSlot()
{
    qDebug() << "setFontSizeSlot";
    int num = m_setSizePage->ui->listWidget->currentRow();
    m_fontLabel->setText(QString::number(num+10));
    m_setSizePage->close();
    update();
    QTextCharFormat fmt;
    fmt.setFontPointSize(num+10);
    mergeFormatOnWordOrSelection(fmt);
}

// 调色板
void EditPage::initColor()
{
    m_colorNum[0] = QColor(SetFontColor::KY_BLUE);
    m_colorNum[1] = QColor(SetFontColor::KY_RED);
    m_colorNum[2] = QColor(SetFontColor::KY_GREEN);
    m_colorNum[3] = QColor(SetFontColor::KY_ORANGE);
    m_colorNum[4] = QColor(SetFontColor::KY_PURPLE);
}

void EditPage::setFontColorSlot(QListWidgetItem *item)
{
    qDebug() << "Sender" << sender();
    qDebug() << "Item" << item;
    int num = m_setColorFontPage->ui->listWidget->currentRow();
    QTextCharFormat fmt;
    if (num != 5) {
        m_defaultFontColorChanged = true;
        fmt.setForeground(m_colorNum[num]);
        ui->textEdit->mergeCurrentCharFormat(fmt);
    } else {
        m_defaultFontColorChanged = false;
        fmt.setForeground(palette().color(QPalette::Text));
        // fmt.clearForeground();
        ui->textEdit->mergeCurrentCharFormat(fmt);
    }
    m_setColorFontPage->close();
}

void EditPage::blueBtnSlot()
{
    QColor color((PaletteWidget::KY_BLUE));
    m_editColor = color;
    emit colorhasChanged(m_editColor, m_noteId);
    m_noteHead->colorWidget = color;
    m_noteHeadMenu->colorWidget = color;
    update();
}

void EditPage::redBtnSlot()
{
    QColor color((PaletteWidget::KY_RED));
    m_editColor = color;
    emit colorhasChanged(m_editColor, m_noteId);
    m_noteHead->colorWidget = color;
    m_noteHeadMenu->colorWidget = color;
    update();
}

void EditPage::darkGreenBtnSlot()
{
    QColor color(PaletteWidget::KY_GREEN);
    m_editColor = color;
    emit colorhasChanged(m_editColor, m_noteId);
    m_noteHead->colorWidget = color;
    m_noteHeadMenu->colorWidget = color;
    update();
}

void EditPage::orangeBtnSlot()
{
    QColor color(PaletteWidget::KY_ORANGE);
    m_editColor = color;
    emit colorhasChanged(m_editColor, m_noteId);
    m_noteHead->colorWidget = color;
    m_noteHeadMenu->colorWidget = color;
    update();
}

void EditPage::purpleBtnSlot()
{
    QColor color(PaletteWidget::KY_PURPLE);
    m_editColor = color;
    emit colorhasChanged(m_editColor, m_noteId);
    m_noteHead->colorWidget = color;
    m_noteHeadMenu->colorWidget = color;
    update();
}

void EditPage::goldenBtnSlot()
{
    QColor color(PaletteWidget::KY_YELLOW);
    m_editColor = color;
    emit colorhasChanged(m_editColor, m_noteId);
    m_noteHead->colorWidget = color;
    m_noteHeadMenu->colorWidget = color;
    update();
}

void EditPage::greyBtnSlot()
{
    QColor color(PaletteWidget::KY_GREY);
    m_editColor = color;
    emit colorhasChanged(m_editColor, m_noteId);
    m_noteHead->colorWidget = color;
    m_noteHeadMenu->colorWidget = color;
    update();
}

void EditPage::lightGreenBtnSlot()
{
    m_editColor = QColor(110, 207, 67);
    emit colorhasChanged(m_editColor, m_noteId);
    m_noteHead->colorWidget = QColor(110, 207, 67);
    m_noteHeadMenu->colorWidget = QColor(110, 207, 67);
    update();
}

void EditPage::yellowBtnSlot()
{
    m_editColor = QColor(144, 101, 255);
    emit colorhasChanged(m_editColor, m_noteId);
    m_noteHead->colorWidget = QColor(144, 101, 255);
    m_noteHeadMenu->colorWidget = QColor(144, 101, 255);
    update();
}

void EditPage::pinkBtnSlot()
{
    QColor color(PaletteWidget::KY_PINK);
    m_editColor = color;
    emit colorhasChanged(m_editColor, m_noteId);
    m_noteHead->colorWidget = color;
    m_noteHeadMenu->colorWidget = color;
    update();
}

void EditPage::defaultBtnSlot()
{
    if (m_notebook->m_isThemeChanged) {
        qDebug() << "白色便签头";
        m_editColor = QColor(255, 255, 255);
        m_noteHead->colorWidget = QColor(255, 255, 255);
        m_noteHeadMenu->colorWidget = QColor(255, 255, 255);
        m_noteHeadMenu->ui->pushButtonExit->setIcon(QPixmap(":/image/1x/close_block.png"));
        m_noteHeadMenu->ui->pushButtonExit->setIconSize(QSize(20, 20));
        m_noteHeadMenu->ui->pushButtonMenu->setIcon(QPixmap(":/image/1x/more_block.png"));
        m_noteHeadMenu->ui->pushButtonMenu->setIconSize(QSize(20, 20));
        m_noteHeadMenu->ui->pushButtonPalette->setIcon(QPixmap(":/image/1x/more_block.png"));
        m_noteHeadMenu->ui->pushButtonPalette->setIconSize(QSize(20, 20));

        QString _Stylesheet;
        QString _BgColor;
        _BgColor = m_editColor.name();
        _Stylesheet = "background-color: %1;";
        _Stylesheet = _Stylesheet.arg(_BgColor);
        m_noteHeadMenu->ui->pushButtonExit->setStyleSheet(_Stylesheet);
        m_noteHeadMenu->ui->pushButtonMenu->setStyleSheet(_Stylesheet);
        m_noteHeadMenu->ui->pushButtonPalette->setStyleSheet(_Stylesheet);
    } else {
        qDebug() << "黑色便签头";
        m_editColor = QColor(0, 0, 0);
        m_noteHead->colorWidget = QColor(0, 0, 0);
        m_noteHeadMenu->colorWidget = QColor(0, 0, 0);
        m_noteHeadMenu->ui->pushButtonExit->setIcon(QPixmap(":/image/1x/close_block.png"));
        m_noteHeadMenu->ui->pushButtonExit->setIconSize(QSize(20, 20));
        m_noteHeadMenu->ui->pushButtonMenu->setIcon(QPixmap(":/image/1x/more_block.png"));
        m_noteHeadMenu->ui->pushButtonMenu->setIconSize(QSize(20, 20));
        m_noteHeadMenu->ui->pushButtonPalette->setIcon(QPixmap(":/image/1x/more_block.png"));
        m_noteHeadMenu->ui->pushButtonPalette->setIconSize(QSize(20, 20));

        QString _Stylesheet;
        QString _BgColor;
        QString _Color;
        _BgColor = m_editColor.name();
        _Color = QColor(19, 20, 20, 0).name();
        _Stylesheet = "background-color: %1; color: %2;";
        _Stylesheet = _Stylesheet.arg(_BgColor).arg(_Color);
        m_noteHeadMenu->ui->pushButtonExit->setStyleSheet(_Stylesheet);
        m_noteHeadMenu->ui->pushButtonMenu->setStyleSheet(_Stylesheet);
        m_noteHeadMenu->ui->pushButtonPalette->setStyleSheet(_Stylesheet);
    }

    emit colorhasChanged(m_editColor, m_noteId);
    update();
}

void EditPage::showFullScreenSlot()
{
    if (!m_isFullscreen) {
        this->showFullScreen();
        m_isFullscreen = true;
    } else {
        this->showNormal();
        m_isFullscreen = false;
    }
}

void EditPage::setWindowKeepAbove()
{
    KWindowSystem::setState(this->winId(), NET::KeepAbove);
}

void EditPage::setWindowStatusClear()
{
    KWindowSystem::clearState(this->winId(), NET::KeepAbove);
}

void EditPage::dropImage(const QImage& image, const QString& format) {
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, format.toLocal8Bit().data());
    buffer.close();
    QByteArray base64 = bytes.toBase64();
    QByteArray base64l;
    for (int i=0; i<base64.size(); i++) 
    {
        base64l.append(base64[i]);
        if (i%80 == 0) 
        {
            base64l.append("\n");
        }
    }

    if(image.isNull())
    {
        return ;
    }

    QTextCursor cursor = ui->textEdit->textCursor();

    if(cursor.atStart())
    {
        m_isInsImg = true;
    }

    QTextImageFormat imageFormat;
    imageFormat.setWidth  ( image.width() );
    imageFormat.setHeight ( image.height() );
    imageFormat.setName   ( QString("data:image/%1;base64,%2")
                                .arg(QString("%1.%2").arg(rand()).arg(format))
                                .arg(base64l.data())
                                );
    cursor.insertImage    ( imageFormat );
}


void EditPage::insertpicture()
{
        QSettings s;
        QString attdir = s.value("general/filedialog-path").toString();
        QString file = QFileDialog::getOpenFileName(this,
                                        tr("Select an image"),
                                        attdir,
                                        tr("JPEG (*.jpg);; GIF (*.gif);; PNG (*.png);; BMP (*.bmp);; All (*)"));
        QImage image = QImageReader(file).read();
        dropImage(image, QFileInfo(file).suffix().toUpper().toLocal8Bit().data());
}

void EditPage::setHints()
{
    kabase::WindowManage::removeHeader(this);
}

void EditPage::defaultTextColorSlot()
{
    QPalette pal;
    m_editColor = pal.text().color();
    emit colorhasChanged(m_editColor, m_noteId);
    m_noteHead->colorWidget = m_editColor;
    m_noteHeadMenu->colorWidget = m_editColor;
    update();
}

void EditPage::onFontColorClicked()
{
    showAnchoredPopup(m_setColorFontPage, ui->styleBtn, true);
}
