#pragma once

#include <QDialog>
#include <QPointer>
#include <QString>

struct ICoreWebView2Environment;
struct ICoreWebView2Controller;
struct ICoreWebView2;

class QLabel;
class QTimer;

// Opens the Shuffle site in an embedded Edge (WebView2) window. The user logs in there; once the
// site has stored the session, the three values below are read from its localStorage and the
// dialog closes by itself:
//   token         JSON.parse(JSON.parse(localStorage.getItem("persist:root")).auth).accessToken
//   correlationId localStorage.getItem('CORRELATION_USER_ID')
//   browserId     localStorage.getItem('BROWSER_ID')
class ShuffleLoginDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ShuffleLoginDialog(const QString &site, QWidget *parent = nullptr);
    ~ShuffleLoginDialog() override;

    QString token() const { return m_token; }
    QString correlationUserId() const { return m_correlationId; }
    QString browserId() const { return m_browserId; }

    static bool isAvailable();

protected:
    void resizeEvent(QResizeEvent *event) override;

private:
    void initializeWebView();
    void pollStorage();
    void updateBounds();

    QLabel *m_status = nullptr;
    QWidget *m_webContainer = nullptr;
    QTimer *m_pollTimer = nullptr;
    bool m_polling = false;

    ICoreWebView2Environment *m_env = nullptr;
    ICoreWebView2Controller *m_controller = nullptr;
    ICoreWebView2 *m_webview = nullptr;

    QString m_site;
    QString m_token;
    QString m_correlationId;
    QString m_browserId;
};
