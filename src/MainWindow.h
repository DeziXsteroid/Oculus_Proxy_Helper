#pragma once

#include "ProxyServer.h"

#include <QComboBox>
#include <QLabel>
#include <QMainWindow>
#include <QPushButton>
#include <QStackedWidget>
#include <QTimer>

class MainWindow final : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);

private:
    struct AddressChoice
    {
        QString title;
        QString detail;
        QHostAddress address;
        bool isAny = false;
    };

    void buildUi();
    QWidget* buildHomePage();
    QWidget* buildUsbPage();
    QWidget* buildVpnPage();
    QWidget* buildQloaderPage();
    QWidget* buildGraphicCpuPage();
    QWidget* buildAdbDriverPage();

    void applyTheme();
    void showHome();
    void showUsbPage();
    void showVpnPage();
    void showQloaderPage();
    void showGraphicCpuPage();
    void showAdbDriverPage();
    void startProxy();
    void refreshAdapters();
    void updateRunningState();
    void updateStats();
    void updateStatusMessage(const QString& message);

    QString headsetProxyAddress() const;
    QString headsetProxyHost() const;
    quint16 selectedPort() const;

    static QString formatBytes(quint64 bytes);
    static QString formatSpeed(quint64 bytesPerSecond);

    ProxyServer m_proxy;
    QVector<AddressChoice> m_outboundChoices;

    QStackedWidget* m_stack = nullptr;
    QLabel* m_statusText = nullptr;
    QLabel* m_addressLabel = nullptr;
    QLabel* m_splitAddressLabel = nullptr;
    QLabel* m_connectionInfoLabel = nullptr;
    QLabel* m_statusMessageLabel = nullptr;
    QComboBox* m_outboundCombo = nullptr;
    QComboBox* m_portCombo = nullptr;
    QPushButton* m_toggleButton = nullptr;
    QPushButton* m_copyButton = nullptr;
    QPushButton* m_refreshButton = nullptr;
    QTimer m_statsTimer;
    quint64 m_lastUploadBytes = 0;
    quint64 m_lastDownloadBytes = 0;
};
