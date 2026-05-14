#include "MainWindow.h"

#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHostAddress>
#include <QIcon>
#include <QIntValidator>
#include <QLineEdit>
#include <QNetworkAddressEntry>
#include <QNetworkInterface>
#include <QPixmap>
#include <QScrollArea>
#include <QSize>
#include <QStyle>
#include <QUrl>
#include <QVBoxLayout>

#include <cstring>

namespace
{
constexpr int HomePageIndex = 0;
constexpr int UsbPageIndex = 1;
constexpr int VpnPageIndex = 2;
constexpr int QloaderPageIndex = 3;
constexpr int GraphicCpuPageIndex = 4;
constexpr int AdbDriverPageIndex = 5;

QLabel* makeLabel(const QString& text, const QString& objectName = {})
{
    auto* label = new QLabel(text);
    if (!objectName.isEmpty()) {
        label->setObjectName(objectName);
    }
    return label;
}

QLabel* makeRichLabel(const QString& text, const QString& objectName = {})
{
    auto* label = makeLabel(text, objectName);
    label->setTextFormat(Qt::RichText);
    label->setOpenExternalLinks(true);
    label->setWordWrap(true);
    return label;
}

QPushButton* makeButton(const QString& text, const QString& objectName = {})
{
    auto* button = new QPushButton(text);
    button->setCursor(Qt::PointingHandCursor);
    if (!objectName.isEmpty()) {
        button->setObjectName(objectName);
    }
    return button;
}

QFrame* makePanel(const QString& objectName = QStringLiteral("panel"))
{
    auto* panel = new QFrame;
    panel->setObjectName(objectName);
    panel->setFrameShape(QFrame::NoFrame);
    return panel;
}

QPushButton* makeBackButton()
{
    auto* button = makeButton(QStringLiteral("←"), QStringLiteral("backButton"));
    button->setFixedSize(38, 34);
    return button;
}

QFrame* makeStep(int number, const QString& title, const QString& body)
{
    auto* row = makePanel(QStringLiteral("stepPanel"));
    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(14, 12, 14, 12);
    layout->setSpacing(12);

    auto* badge = makeLabel(QString::number(number), QStringLiteral("stepBadge"));
    badge->setAlignment(Qt::AlignCenter);
    badge->setFixedSize(30, 30);

    auto* textBox = new QVBoxLayout;
    textBox->setSpacing(3);
    textBox->addWidget(makeLabel(title, QStringLiteral("stepTitle")));
    textBox->addWidget(makeRichLabel(body, QStringLiteral("stepText")));

    layout->addWidget(badge, 0, Qt::AlignTop);
    layout->addLayout(textBox, 1);
    return row;
}

QWidget* makeHomeAction(int number, const QString& title, const QString& iconPath)
{
    auto* card = new QWidget;
    card->setFixedHeight(86);
    auto* layout = new QVBoxLayout(card);
    layout->setContentsMargins(16, 0, 16, 6);
    layout->setSpacing(9);

    auto* badge = makeLabel(QString::number(number), QStringLiteral("homeBadge"));
    badge->setAlignment(Qt::AlignCenter);
    badge->setFixedSize(26, 26);

    auto* button = makeButton(title, QStringLiteral("homeButton"));
    button->setIcon(QIcon(iconPath));
    button->setIconSize(QSize(22, 22));
    button->setFixedHeight(44);
    button->setFixedWidth(245);
    button->setProperty("actionTitle", title);

    layout->addWidget(badge, 0, Qt::AlignHCenter);
    layout->addWidget(button, 0, Qt::AlignHCenter);
    return card;
}

QPushButton* homeActionButton(QWidget* card)
{
    return card->findChild<QPushButton*>(QStringLiteral("homeButton"), Qt::FindDirectChildrenOnly);
}

bool isUsableIPv4(const QHostAddress& address)
{
    return address.protocol() == QAbstractSocket::IPv4Protocol
        && !address.isLoopback()
        && !address.isNull();
}

bool isPrivateIPv4(const QHostAddress& address)
{
    const quint32 ip = address.toIPv4Address();
    return (ip & 0xff000000u) == 0x0a000000u
        || (ip & 0xfff00000u) == 0xac100000u
        || (ip & 0xffff0000u) == 0xc0a80000u;
}

bool isLikelyVpnInterface(const QNetworkInterface& iface)
{
    const QString text = QStringLiteral("%1 %2")
                             .arg(iface.name(), iface.humanReadableName())
                             .toLower();

    static const QStringList markers = {
        QStringLiteral("vpn"),
        QStringLiteral("wireguard"),
        QStringLiteral("wintun"),
        QStringLiteral("openvpn"),
        QStringLiteral("tap"),
        QStringLiteral("tun"),
        QStringLiteral("tailscale"),
        QStringLiteral("proton"),
        QStringLiteral("nord"),
        QStringLiteral("surfshark"),
        QStringLiteral("ppp")
    };

    for (const QString& marker : markers) {
        if (text.contains(marker)) {
            return true;
        }
    }

    return false;
}

QString adapterDisplayName(const QNetworkInterface& iface)
{
    return iface.humanReadableName().isEmpty() ? iface.name() : iface.humanReadableName();
}

QHostAddress bestClientFacingAddress(const QHostAddress& selectedOutbound)
{
    QHostAddress fallback;

    for (const QNetworkInterface& iface : QNetworkInterface::allInterfaces()) {
        const auto flags = iface.flags();
        if (!flags.testFlag(QNetworkInterface::IsUp)
            || !flags.testFlag(QNetworkInterface::IsRunning)
            || flags.testFlag(QNetworkInterface::IsLoopBack)
            || isLikelyVpnInterface(iface)) {
            continue;
        }

        for (const QNetworkAddressEntry& entry : iface.addressEntries()) {
            const QHostAddress address = entry.ip();
            if (!isUsableIPv4(address) || address == selectedOutbound) {
                continue;
            }
            if (fallback.isNull()) {
                fallback = address;
            }
            if (isPrivateIPv4(address)) {
                return address;
            }
        }
    }

    return fallback.isNull() ? selectedOutbound : fallback;
}
}

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    buildUi();
    applyTheme();
    refreshAdapters();
    updateRunningState();
    updateStats();

    connect(&m_proxy, &ProxyServer::runningChanged, this, &MainWindow::updateRunningState);
    connect(&m_proxy, &ProxyServer::logMessage, this, &MainWindow::updateStatusMessage);

    m_statsTimer.setInterval(1000);
    connect(&m_statsTimer, &QTimer::timeout, this, &MainWindow::updateStats);
    m_statsTimer.start();
}

void MainWindow::buildUi()
{
    setWindowTitle(QStringLiteral("Oculus SetUP"));
    setWindowIcon(QIcon(QStringLiteral(":/icons/app.png")));
    setFixedSize(370, 540);

    m_stack = new QStackedWidget(this);
    setCentralWidget(m_stack);
    m_stack->addWidget(buildHomePage());
    m_stack->addWidget(buildUsbPage());
    m_stack->addWidget(buildVpnPage());
    m_stack->addWidget(buildQloaderPage());
    m_stack->addWidget(buildGraphicCpuPage());
    m_stack->addWidget(buildAdbDriverPage());
}

QWidget* MainWindow::buildHomePage()
{
    auto* page = new QWidget;
    auto* root = new QVBoxLayout(page);
    root->setContentsMargins(26, 8, 26, 2);
    root->setSpacing(0);
    root->setAlignment(Qt::AlignTop);

    auto* title = makeLabel(QStringLiteral("Oculus SetUP"), QStringLiteral("heroTitle"));
    title->setAlignment(Qt::AlignCenter);

    auto* usb = makeHomeAction(1, QStringLiteral("Update by USB"), QStringLiteral(":/icons/usb.svg"));
    auto* vpn = makeHomeAction(2, QStringLiteral("PROXY CREATE"), QStringLiteral(":/icons/vpn.svg"));
    auto* qloader = makeHomeAction(3, QStringLiteral("QLoader"), QStringLiteral(":/icons/loader.svg"));
    auto* graphicCpu = makeHomeAction(4, QStringLiteral("Graphic/CPU"), QStringLiteral(":/icons/loader.svg"));
    auto* adbDriver = makeHomeAction(5, QStringLiteral("ADB/Driver Helper"), QStringLiteral(":/icons/usb.svg"));

    connect(homeActionButton(usb), &QPushButton::clicked, this, &MainWindow::showUsbPage);
    connect(homeActionButton(vpn), &QPushButton::clicked, this, &MainWindow::showVpnPage);
    connect(homeActionButton(qloader), &QPushButton::clicked, this, &MainWindow::showQloaderPage);
    connect(homeActionButton(graphicCpu), &QPushButton::clicked, this, &MainWindow::showGraphicCpuPage);
    connect(homeActionButton(adbDriver), &QPushButton::clicked, this, &MainWindow::showAdbDriverPage);

    root->addWidget(title);
    root->addSpacing(12);
    root->addWidget(usb);
    root->addWidget(vpn);
    root->addWidget(qloader);
    root->addWidget(graphicCpu);
    root->addWidget(adbDriver);
    return page;
}

QWidget* MainWindow::buildUsbPage()
{
    auto* page = new QWidget;
    auto* root = new QVBoxLayout(page);
    root->setContentsMargins(18, 18, 18, 18);
    root->setSpacing(10);

    auto* header = new QHBoxLayout;
    auto* back = makeBackButton();
    connect(back, &QPushButton::clicked, this, &MainWindow::showHome);
    auto* titleBlock = new QVBoxLayout;
    titleBlock->setSpacing(2);
    titleBlock->addWidget(makeLabel(QStringLiteral("Update by USB"), QStringLiteral("pageTitle")));
    titleBlock->addWidget(makeLabel(QStringLiteral("Официальное обновление Meta Quest"), QStringLiteral("pageSubtitle")));
    header->addWidget(back, 0, Qt::AlignTop);
    header->addLayout(titleBlock, 1);

    root->addLayout(header);
    root->addWidget(makeStep(1,
                             QStringLiteral("Откройте сайт Meta"),
                             QStringLiteral("Перейдите на <a href=\"https://www.meta.com/help/quest/software_update/\">Meta Quest Software Update Tool</a> и выберите модель своих очков.")));
    root->addWidget(makeStep(2,
                             QStringLiteral("Подключите очки"),
                             QStringLiteral("Соедините очки с компьютером USB‑C кабелем. Если браузер спросит доступ к устройству, выберите Meta Quest.")));
    root->addWidget(makeStep(3,
                             QStringLiteral("Запустите обновление"),
                             QStringLiteral("Нажмите кнопку обновления на сайте и дождитесь завершения. Кабель лучше не трогать до конца процесса.")));
    root->addStretch();
    return page;
}

QWidget* MainWindow::buildVpnPage()
{
    auto* page = new QWidget;
    auto* root = new QVBoxLayout(page);
    root->setContentsMargins(18, 18, 18, 18);
    root->setSpacing(10);

    auto* header = new QHBoxLayout;
    auto* back = makeBackButton();
    connect(back, &QPushButton::clicked, this, &MainWindow::showHome);
    auto* titleBlock = new QVBoxLayout;
    titleBlock->setSpacing(2);
    titleBlock->addWidget(makeLabel(QStringLiteral("PROXY CREATE"), QStringLiteral("pageTitle")));
    titleBlock->addWidget(makeLabel(QStringLiteral("Настройка прокси для очков"), QStringLiteral("pageSubtitle")));
    header->addWidget(back, 0, Qt::AlignTop);
    header->addLayout(titleBlock, 1);
    root->addLayout(header);

    auto* settings = makePanel();
    auto* settingsLayout = new QGridLayout(settings);
    settingsLayout->setContentsMargins(14, 14, 14, 14);
    settingsLayout->setHorizontalSpacing(8);
    settingsLayout->setVerticalSpacing(10);

    settingsLayout->addWidget(makeLabel(QStringLiteral("Адаптер"), QStringLiteral("fieldLabel")), 0, 0, 1, 3);
    m_refreshButton = makeButton(QStringLiteral("↻"), QStringLiteral("refreshButton"));
    m_refreshButton->setFixedSize(48, 42);
    connect(m_refreshButton, &QPushButton::clicked, this, &MainWindow::refreshAdapters);
    m_outboundCombo = new QComboBox;
    settingsLayout->addWidget(m_refreshButton, 1, 0);
    settingsLayout->addWidget(m_outboundCombo, 1, 1, 1, 2);

    settingsLayout->addWidget(makeLabel(QStringLiteral("Порт"), QStringLiteral("fieldLabel")), 2, 0);
    m_portCombo = new QComboBox;
    m_portCombo->setEditable(true);
    m_portCombo->setInsertPolicy(QComboBox::NoInsert);
    m_portCombo->addItems({QStringLiteral("8888"),
                           QStringLiteral("8080"),
                           QStringLiteral("3128"),
                           QStringLiteral("8000"),
                           QStringLiteral("8118")});
    m_portCombo->lineEdit()->setValidator(new QIntValidator(1, 65535, m_portCombo));
    settingsLayout->addWidget(m_portCombo, 2, 1);

    m_toggleButton = makeButton(QStringLiteral("Включить"), QStringLiteral("primaryButton"));
    settingsLayout->addWidget(m_toggleButton, 2, 2);
    settingsLayout->setColumnStretch(1, 1);
    root->addWidget(settings);

    auto* proxyBox = makePanel();
    auto* proxyLayout = new QVBoxLayout(proxyBox);
    proxyLayout->setContentsMargins(14, 14, 14, 14);
    proxyLayout->setSpacing(7);
    proxyLayout->addWidget(makeLabel(QStringLiteral("Ввести в очках"), QStringLiteral("fieldLabel")));
    m_addressLabel = makeLabel(QStringLiteral("—"), QStringLiteral("address"));
    m_addressLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_splitAddressLabel = makeLabel(QStringLiteral("Hostname: —    Port: —"), QStringLiteral("splitAddress"));
    m_splitAddressLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_copyButton = makeButton(QStringLiteral("Копировать IP:PORT"));
    m_copyButton->setEnabled(false);
    proxyLayout->addWidget(m_addressLabel);
    proxyLayout->addWidget(m_splitAddressLabel);
    proxyLayout->addSpacing(4);
    proxyLayout->addWidget(makeLabel(QStringLiteral("Подключения"), QStringLiteral("fieldLabel")));
    m_connectionInfoLabel = makeLabel(QStringLiteral("0 подключений • ↓ 0 B/s • ↑ 0 B/s"), QStringLiteral("connectionInfo"));
    m_connectionInfoLabel->setWordWrap(true);
    proxyLayout->addWidget(m_connectionInfoLabel);
    proxyLayout->addWidget(m_copyButton);
    root->addWidget(proxyBox);

    m_statusMessageLabel = makeLabel(QStringLiteral("Требуется хороший включенный VPN на компьютере и включенный режим TUN."), QStringLiteral("softNote"));
    m_statusMessageLabel->setWordWrap(true);
    root->addWidget(m_statusMessageLabel);
    root->addStretch();

    connect(m_toggleButton, &QPushButton::clicked, this, [this]() {
        if (m_proxy.isRunning()) {
            m_proxy.stop();
            updateStatusMessage(QStringLiteral("Прокси остановлен."));
            return;
        }
        startProxy();
    });
    connect(m_copyButton, &QPushButton::clicked, this, [this]() {
        QApplication::clipboard()->setText(headsetProxyAddress());
        updateStatusMessage(QStringLiteral("Скопировано: %1.").arg(headsetProxyAddress()));
    });
    connect(m_outboundCombo, &QComboBox::currentIndexChanged, this, [this](int) {
        updateRunningState();
    });
    connect(m_portCombo, &QComboBox::currentTextChanged, this, [this](const QString&) {
        updateRunningState();
    });

    return page;
}

QWidget* MainWindow::buildQloaderPage()
{
    auto* page = new QWidget;
    auto* root = new QVBoxLayout(page);
    root->setContentsMargins(18, 18, 18, 18);
    root->setSpacing(10);

    auto* header = new QHBoxLayout;
    auto* back = makeBackButton();
    connect(back, &QPushButton::clicked, this, &MainWindow::showHome);
    auto* titleBlock = new QVBoxLayout;
    titleBlock->setSpacing(2);
    titleBlock->addWidget(makeLabel(QStringLiteral("QLoader"), QStringLiteral("pageTitle")));
    titleBlock->addWidget(makeLabel(QStringLiteral("Загрузка и личный сертификат"), QStringLiteral("pageSubtitle")));
    header->addWidget(back, 0, Qt::AlignTop);
    header->addLayout(titleBlock, 1);

    root->addLayout(header);
    root->addWidget(makeStep(1,
                             QStringLiteral("Скачайте QLoader"),
                             QStringLiteral("Откройте страницу загрузки: <a href=\"https://cloud.dipvr.ru/download/trailers/\">cloud.dipvr.ru/download/trailers</a> и скачайте архив.")));
    root->addWidget(makeStep(2,
                             QStringLiteral("Подпишитесь на Telegram"),
                             QStringLiteral("Подпишитесь на канал <a href=\"https://t.me/VRGamesRUS\">t.me/VRGamesRUS</a>, чтобы бот выдал доступ.")));
    root->addWidget(makeStep(3,
                             QStringLiteral("Получите сертификат"),
                             QStringLiteral("Напишите боту <a href=\"https://t.me/qlfixbot\">@qlfixbot</a> и получите личный сертификат.")));
    root->addWidget(makeStep(4,
                             QStringLiteral("Перекиньте сертификат"),
                             QStringLiteral("Скопируйте полученный сертификат в папку с QLoader, рядом с исполняемым файлом.")));
    root->addStretch();
    return page;
}

QWidget* MainWindow::buildGraphicCpuPage()
{
    auto* page = new QWidget;
    auto* root = new QVBoxLayout(page);
    root->setContentsMargins(18, 18, 18, 18);
    root->setSpacing(10);

    auto* header = new QHBoxLayout;
    auto* back = makeBackButton();
    connect(back, &QPushButton::clicked, this, &MainWindow::showHome);
    auto* titleBlock = new QVBoxLayout;
    titleBlock->setSpacing(2);
    titleBlock->addWidget(makeLabel(QStringLiteral("Graphic/CPU"), QStringLiteral("pageTitle")));
    titleBlock->addWidget(makeLabel(QStringLiteral("Настройка производительности"), QStringLiteral("pageSubtitle")));
    header->addWidget(back, 0, Qt::AlignTop);
    header->addLayout(titleBlock, 1);

    root->addLayout(header);
    root->addWidget(makeStep(1,
                             QStringLiteral("Установите SideQuest"),
                             QStringLiteral("Откройте <a href=\"https://sidequestvr.com/setup-howto\">sidequestvr.com/setup-howto</a> и установите SideQuest на компьютер.")));
    root->addWidget(makeStep(2,
                             QStringLiteral("Подключите VR"),
                             QStringLiteral("Включите ADB/Developer Mode, подключите очки кабелем и запустите SideQuest.")));
    root->addWidget(makeStep(3,
                             QStringLiteral("Откройте настройки"),
                             QStringLiteral("Справа сверху нажмите иконку с гаечным ключом и выставьте нужные параметры CPU/GPU.")));
    root->addWidget(makeStep(4,
                             QStringLiteral("Важно"),
                             QStringLiteral("Повышение CPU и графики влияет на батарею, FPS, нагрев и стабильность. Используйте на свой риск, мы не несем ответственности за последствия.")));
    root->addStretch();
    return page;
}

QWidget* MainWindow::buildAdbDriverPage()
{
    auto* page = new QWidget;
    auto* root = new QVBoxLayout(page);
    root->setContentsMargins(18, 18, 18, 18);
    root->setSpacing(8);

    auto* header = new QHBoxLayout;
    auto* back = makeBackButton();
    connect(back, &QPushButton::clicked, this, &MainWindow::showHome);
    auto* titleBlock = new QVBoxLayout;
    titleBlock->setSpacing(2);
    titleBlock->addWidget(makeLabel(QStringLiteral("ADB/Driver Helper"), QStringLiteral("pageTitle")));
    titleBlock->addWidget(makeLabel(QStringLiteral("Если нет уведомления авторизации ADB"), QStringLiteral("pageSubtitle")));
    header->addWidget(back, 0, Qt::AlignTop);
    header->addLayout(titleBlock, 1);

    root->addLayout(header);
    auto* scroll = new QScrollArea;
    scroll->setObjectName(QStringLiteral("scrollArea"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);

    auto* content = new QWidget;
    auto* steps = new QVBoxLayout(content);
    steps->setContentsMargins(0, 0, 0, 0);
    steps->setSpacing(8);
    steps->addWidget(makeStep(1,
                              QStringLiteral("Отключите очки"),
                              QStringLiteral("Отсоедините VR-очки от компьютера по USB.")));
    steps->addWidget(makeStep(2,
                              QStringLiteral("Скачайте драйвер"),
                              QStringLiteral("Откройте <a href=\"https://developers.meta.com/horizon/downloads/package/oculus-adb-drivers/?locale=ru_RU\">Oculus ADB Drivers</a> на сайте Meta и скачайте ZIP.")));
    steps->addWidget(makeStep(3,
                              QStringLiteral("Разархивируйте ZIP"),
                              QStringLiteral("Распакуйте папку из архива в удобное место.")));
    steps->addWidget(makeStep(4,
                              QStringLiteral("Установите INF"),
                              QStringLiteral("Нажмите правой кнопкой мыши на <b>android_winusb.inf</b> и выберите <b>Установить</b>.")));
    steps->addWidget(makeStep(5,
                              QStringLiteral("Перезагрузите ПК"),
                              QStringLiteral("После установки драйвера перезагрузите компьютер.")));
    steps->addWidget(makeStep(6,
                              QStringLiteral("Подключите очки"),
                              QStringLiteral("Подключите очки кабелем к ПК и проверьте уведомление авторизации ADB в шлеме.")));
    steps->addStretch();
    scroll->setWidget(content);
    root->addWidget(scroll, 1);
    return page;
}

void MainWindow::applyTheme()
{
    setStyleSheet(R"(
        QMainWindow, QStackedWidget {
            background: #0f0f0f;
        }
        QWidget {
            color: #f2f2f2;
            font-family: "Segoe UI";
            font-size: 10pt;
        }
        #heroTitle {
            font-size: 24px;
            font-weight: 750;
        }
        #heroSubtitle, #pageSubtitle, #homeHint, #softNote, #splitAddress, #connectionInfo {
            color: #9c9c9c;
            font-size: 12px;
        }
        #versionText {
            color: #626262;
            font-size: 11px;
        }
        #panel, #stepPanel {
            background: #171717;
            border: 1px solid #2a2a2a;
            border-radius: 12px;
        }
        #homeBadge, #stepBadge {
            background: #f4f4f4;
            color: #0f0f0f;
            font-weight: 750;
        }
        #homeBadge {
            border-radius: 13px;
            font-size: 15px;
        }
        #stepBadge {
            border-radius: 15px;
            font-size: 16px;
        }
        #homeButton {
            background: #202020;
            border: 1px solid #373737;
            border-radius: 10px;
            padding: 8px 14px;
            font-size: 14px;
            font-weight: 700;
            text-align: center;
        }
        #homeButton:hover, QPushButton:hover {
            background: #2a2a2a;
        }
        #pageTitle {
            font-size: 22px;
            font-weight: 750;
        }
        #backButton {
            background: transparent;
            border: none;
            font-size: 24px;
            padding: 0;
        }
        #fieldLabel, #stepTitle {
            color: #e8e8e8;
            font-size: 12px;
            font-weight: 700;
        }
        #stepText {
            color: #b8b8b8;
            font-size: 12px;
            line-height: 130%;
        }
        #smallLabel {
            color: #8f8f8f;
            font-size: 11px;
        }
        #address {
            color: #ffffff;
            font-size: 24px;
            font-weight: 700;
        }
        #metric {
            color: #ffffff;
            font-size: 24px;
            font-weight: 700;
        }
        #value {
            color: #d0d0d0;
            font-size: 13px;
            font-weight: 600;
        }
        #statusText {
            color: #d8d8d8;
            background: #171717;
            border: 1px solid #303030;
            border-radius: 10px;
            padding: 8px 10px;
            font-weight: 700;
        }
        #statusText[running="true"] {
            border-color: #707070;
        }
        QPushButton {
            background: #202020;
            color: #eeeeee;
            border: 1px solid #3a3a3a;
            border-radius: 9px;
            padding: 8px 12px;
            font-weight: 650;
        }
        #primaryButton {
            background: #f1f1f1;
            color: #101010;
            border: 1px solid #f1f1f1;
        }
        #refreshButton {
            background: transparent;
            border: none;
            font-size: 25px;
            padding: 0;
        }
        QComboBox {
            background: #101010;
            color: #eeeeee;
            border: 1px solid #373737;
            border-radius: 9px;
            padding: 8px 28px 8px 9px;
            min-height: 24px;
        }
        QComboBox::drop-down {
            subcontrol-origin: padding;
            subcontrol-position: top right;
            width: 28px;
            border: none;
            border-top-right-radius: 9px;
            border-bottom-right-radius: 9px;
        }
        QComboBox::down-arrow {
            width: 0;
            height: 0;
            border: none;
        }
        QComboBox:disabled, QPushButton:disabled {
            color: #777777;
            background: #171717;
            border-color: #292929;
        }
        QComboBox QAbstractItemView {
            background: #181818;
            color: #eeeeee;
            selection-background-color: #2e2e2e;
            border: 1px solid #333333;
            outline: 0;
        }
        QLineEdit {
            background: transparent;
            color: #eeeeee;
            border: none;
        }
        #scrollArea {
            background: transparent;
            border: none;
        }
        #scrollArea QWidget {
            background: transparent;
        }
        QScrollBar:vertical {
            background: transparent;
            width: 8px;
            margin: 0;
        }
        QScrollBar::handle:vertical {
            background: #3a3a3a;
            border-radius: 4px;
            min-height: 24px;
        }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {
            height: 0;
        }
        a {
            color: #ffffff;
            text-decoration: underline;
        }
    )");
}

void MainWindow::showHome()
{
    m_stack->setCurrentIndex(HomePageIndex);
}

void MainWindow::showUsbPage()
{
    m_stack->setCurrentIndex(UsbPageIndex);
}

void MainWindow::showVpnPage()
{
    m_stack->setCurrentIndex(VpnPageIndex);
    updateRunningState();
}

void MainWindow::showQloaderPage()
{
    m_stack->setCurrentIndex(QloaderPageIndex);
}

void MainWindow::showGraphicCpuPage()
{
    m_stack->setCurrentIndex(GraphicCpuPageIndex);
}

void MainWindow::showAdbDriverPage()
{
    m_stack->setCurrentIndex(AdbDriverPageIndex);
}

void MainWindow::startProxy()
{
    ProxyServer::Settings settings;
    settings.listenAddress = QHostAddress::AnyIPv4;
    settings.port = selectedPort();
    settings.outboundAddress = QHostAddress();

    QString error;
    if (!m_proxy.start(settings, &error)) {
        updateStatusMessage(QStringLiteral("Ошибка запуска: %1").arg(error));
    }
}

void MainWindow::refreshAdapters()
{
    const QHostAddress previousOutbound = m_outboundChoices.value(m_outboundCombo->currentIndex()).address;

    m_outboundChoices.clear();
    m_outboundCombo->clear();

    QVector<AddressChoice> vpnChoices;
    QVector<AddressChoice> otherChoices;

    AddressChoice autoChoice;
    autoChoice.title = QStringLiteral("Авто через Windows/VPN");
    autoChoice.detail = QStringLiteral("рекомендуется");
    autoChoice.address = QHostAddress();
    autoChoice.isAny = true;
    m_outboundChoices.push_back(autoChoice);

    for (const QNetworkInterface& iface : QNetworkInterface::allInterfaces()) {
        const auto flags = iface.flags();
        if (!flags.testFlag(QNetworkInterface::IsUp)
            || !flags.testFlag(QNetworkInterface::IsRunning)
            || flags.testFlag(QNetworkInterface::IsLoopBack)) {
            continue;
        }

        for (const QNetworkAddressEntry& entry : iface.addressEntries()) {
            const QHostAddress address = entry.ip();
            if (!isUsableIPv4(address)) {
                continue;
            }

            AddressChoice choice;
            choice.title = adapterDisplayName(iface);
            choice.detail = address.toString();
            choice.address = address;

            if (isLikelyVpnInterface(iface)) {
                vpnChoices.push_back(choice);
            } else {
                otherChoices.push_back(choice);
            }
        }
    }

    m_outboundChoices += vpnChoices;
    m_outboundChoices += otherChoices;

    int outboundIndex = 0;
    for (int i = 0; i < m_outboundChoices.size(); ++i) {
        const auto& choice = m_outboundChoices.at(i);
        m_outboundCombo->addItem(QStringLiteral("%1  (%2)").arg(choice.title, choice.detail));
        if (choice.address == previousOutbound) {
            outboundIndex = i;
        }
    }

    if (m_outboundChoices.size() == 1 && vpnChoices.isEmpty() && otherChoices.isEmpty()) {
        m_outboundCombo->addItem(QStringLiteral("IPv4-адаптеры не найдены"));
        m_toggleButton->setEnabled(false);
        updateStatusMessage(QStringLiteral("Нет доступных IPv4-адаптеров."));
    } else {
        m_outboundCombo->setCurrentIndex(outboundIndex);
        m_toggleButton->setEnabled(true);
    }

    updateRunningState();
}

void MainWindow::updateRunningState()
{
    const bool running = m_proxy.isRunning();

    m_toggleButton->setText(running ? QStringLiteral("Остановить") : QStringLiteral("Включить"));

    m_outboundCombo->setEnabled(!running && !m_outboundChoices.isEmpty());
    m_portCombo->setEnabled(!running);
    m_refreshButton->setEnabled(!running);
    m_copyButton->setEnabled(running);
    m_addressLabel->setText(running ? headsetProxyAddress() : QStringLiteral("—"));
    m_splitAddressLabel->setText(running
                                     ? QStringLiteral("Hostname: %1    Port: %2").arg(headsetProxyHost()).arg(selectedPort())
                                     : QStringLiteral("Hostname: —    Port: —"));

    if (running) {
        updateStatusMessage(QStringLiteral("Требуется хороший включенный VPN на компьютере и включенный режим TUN. В очках укажите Hostname и Port ниже."));
    }
}

void MainWindow::updateStats()
{
    const quint64 upload = m_proxy.totalUploadBytes();
    const quint64 download = m_proxy.totalDownloadBytes();
    const quint64 uploadRate = upload >= m_lastUploadBytes ? upload - m_lastUploadBytes : 0;
    const quint64 downloadRate = download >= m_lastDownloadBytes ? download - m_lastDownloadBytes : 0;
    m_lastUploadBytes = upload;
    m_lastDownloadBytes = download;

    if (m_connectionInfoLabel != nullptr) {
        m_connectionInfoLabel->setText(QStringLiteral("%1 подключений • ↓ %2 • ↑ %3 • всего ↓ %4 / ↑ %5")
                                           .arg(m_proxy.activeConnections())
                                           .arg(formatSpeed(downloadRate), formatSpeed(uploadRate), formatBytes(download), formatBytes(upload)));
    }
}

void MainWindow::updateStatusMessage(const QString& message)
{
    if (m_statusMessageLabel != nullptr && !message.isEmpty()) {
        m_statusMessageLabel->setText(message);
    }
}

QString MainWindow::headsetProxyAddress() const
{
    return QStringLiteral("%1:%2").arg(headsetProxyHost()).arg(selectedPort());
}

QString MainWindow::headsetProxyHost() const
{
    const AddressChoice selected = m_outboundChoices.value(m_outboundCombo->currentIndex());
    const QHostAddress address = bestClientFacingAddress(selected.address);
    return address.isNull() ? QStringLiteral("IP_ПК") : address.toString();
}

quint16 MainWindow::selectedPort() const
{
    bool ok = false;
    const int value = m_portCombo->currentText().trimmed().toInt(&ok);
    if (!ok || value <= 0 || value > 65535) {
        return 8888;
    }
    return static_cast<quint16>(value);
}

QString MainWindow::formatBytes(quint64 bytes)
{
    static constexpr double KiB = 1024.0;
    static constexpr double MiB = KiB * 1024.0;
    static constexpr double GiB = MiB * 1024.0;

    if (bytes >= static_cast<quint64>(GiB)) {
        return QStringLiteral("%1 GB").arg(bytes / GiB, 0, 'f', 2);
    }
    if (bytes >= static_cast<quint64>(MiB)) {
        return QStringLiteral("%1 MB").arg(bytes / MiB, 0, 'f', 2);
    }
    if (bytes >= static_cast<quint64>(KiB)) {
        return QStringLiteral("%1 KB").arg(bytes / KiB, 0, 'f', 1);
    }
    return QStringLiteral("%1 B").arg(bytes);
}

QString MainWindow::formatSpeed(quint64 bytesPerSecond)
{
    return QStringLiteral("%1/s").arg(formatBytes(bytesPerSecond));
}
