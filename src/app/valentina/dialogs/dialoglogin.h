#ifndef DIALOGLOGIN_H
#define DIALOGLOGIN_H

#include <QDialog>
#include <QDateTime>
#include <QString>

class QLineEdit;
class QLabel;
class QPushButton;

struct AuthenticatedUser
{
    QString name{};
    QString email{};

    auto IsValid() const -> bool { return !email.isEmpty(); }
};

class DialogLogin : public QDialog
{
    Q_OBJECT

public:
    explicit DialogLogin(QWidget *parent = nullptr);
    static auto EnsureAuthenticated(QWidget *parent = nullptr) -> bool;
    static auto FetchCurrentUser(AuthenticatedUser &user, QString &resultText, QString &errorMessage) -> bool;
    static auto LogoutCurrentUser(QString &resultText, QString &errorMessage) -> bool;
    static auto AccessTokenExpiration() -> QDateTime;
    static auto RefreshTokenExpiration() -> QDateTime;

private slots:
    void ValidateCredentials();

private:
    static auto TryRestoreSession(QString &errorMessage) -> bool;
    static auto TokenExpiration(const QString &token) -> QDateTime;
    static auto PersistedAccessExpiration() -> QDateTime;
    static auto PersistedRefreshExpiration() -> QDateTime;
    static auto AccessToken() -> QString;
    static auto PersistedUser() -> AuthenticatedUser;
    static auto PersistTokens(const QString &accessToken, const QString &refreshToken, qint64 accessExpiresInSeconds,
                              qint64 refreshExpiresInSeconds, const AuthenticatedUser &user) -> void;
    static auto ClearPersistedTokens() -> void;

    QPushButton *m_signInButton;
    QLineEdit *m_emailLineEdit;
    QLineEdit *m_passwordLineEdit;
    QLabel *m_errorLabel;
};

#endif // DIALOGLOGIN_H
