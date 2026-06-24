#include "dialoglogin.h"

#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QEventLoop>
#include <QFormLayout>
#include <QAuthenticator>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPushButton>
#include <QSettings>
#include <QTimeZone>
#include <QVBoxLayout>

namespace
{
const auto kLoginUrl = QStringLiteral("https://www.yourdomain.com/login");
const auto kRefreshUrl = QStringLiteral("https://www.yourdomain.com/refresh");
const auto kLogoutUrl = QStringLiteral("https://www.yourdomain.com/logout");

const auto kSettingsAuthGroup = QStringLiteral("auth");
const auto kSettingsAccessToken = QStringLiteral("access_token");
const auto kSettingsRefreshToken = QStringLiteral("refresh_token");
const auto kSettingsAccessTokenExpiresAt = QStringLiteral("access_token_expires_at");
const auto kSettingsRefreshTokenExpiresAt = QStringLiteral("refresh_token_expires_at");
const auto kSettingsUserName = QStringLiteral("user_name");
const auto kSettingsUserEmail = QStringLiteral("user_email");

constexpr qint64 kRefreshGraceSeconds = 5 * 24 * 60 * 60; // 5 days

auto DecodeBase64Url(QString data) -> QByteArray
{
    data.replace(u'-', u'+');
    data.replace(u'_', u'/');

    const int padding = data.size() % 4;
    if (padding != 0)
    {
        data.append(QString(4 - padding, u'='));
    }

    return QByteArray::fromBase64(data.toUtf8(), QByteArray::Base64Encoding);
}

auto SendJsonRequest(const QString &url,
                     const QByteArray &method,
                     const QString &bearerToken,
                     const QJsonObject &payload,
                     QString &errorMessage) -> QJsonObject
{
    QNetworkAccessManager manager;
    QObject::connect(&manager,
                     &QNetworkAccessManager::authenticationRequired,
                     &manager,
                     [](QNetworkReply *, QAuthenticator *authenticator)
                     {
                         if (authenticator != nullptr)
                         {
                             authenticator->setUser(QString());
                             authenticator->setPassword(QString());
                         }
                     });
    QNetworkRequest request{QUrl(url)};
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("ModaMaker/%1").arg(QCoreApplication::applicationVersion()));
    request.setRawHeader("Accept", "application/json");
    if (!bearerToken.isEmpty())
    {
        request.setRawHeader("Authorization", "Bearer " + bearerToken.toUtf8());
    }

    QEventLoop loop;
    QNetworkReply *reply = nullptr;

    if (method == QByteArrayLiteral("GET"))
    {
        reply = manager.get(request);
    }
    else if (method == QByteArrayLiteral("POST"))
    {
        reply = manager.post(request, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    }
    else
    {
        reply = manager.sendCustomRequest(request, method, QJsonDocument(payload).toJson(QJsonDocument::Compact));
    }

    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    const QByteArray responseData = reply->readAll();
    const auto networkError = reply->error();
    const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    reply->deleteLater();

    QJsonParseError parseError{};
    const QJsonDocument jsonDocument = QJsonDocument::fromJson(responseData, &parseError);
    const QJsonObject response = jsonDocument.isObject() ? jsonDocument.object() : QJsonObject{};

    if (networkError != QNetworkReply::NoError)
    {
        errorMessage = response.value(QStringLiteral("message")).toString();
        if (errorMessage.isEmpty())
        {
            const QString replyError = reply->errorString();
            errorMessage = replyError.isEmpty()
                               ? QObject::tr("Request failed. Please check your connection and try again.")
                               : QObject::tr("Request failed: %1").arg(replyError);
        }
        return {};
    }

    if (statusCode < 200 || statusCode >= 300)
    {
        errorMessage = response.value(QStringLiteral("message")).toString();
        if (errorMessage.isEmpty())
        {
            const QString rawResponse = QString::fromUtf8(responseData).trimmed();
            if (!rawResponse.isEmpty())
            {
                errorMessage = QObject::tr("Server returned status %1: %2").arg(statusCode).arg(rawResponse);
            }
            else
            {
                errorMessage = QObject::tr("Server returned status %1.").arg(statusCode);
            }
        }
        return {};
    }

    if (!responseData.isEmpty() && (parseError.error != QJsonParseError::NoError || !jsonDocument.isObject()))
    {
        errorMessage = QObject::tr("Invalid response from authentication server.");
        return {};
    }

    return response;
}

auto PostJson(const QString &url, const QJsonObject &payload, QString &errorMessage) -> QJsonObject
{
    return SendJsonRequest(url, QByteArrayLiteral("POST"), QString(), payload, errorMessage);
}

auto ResponseText(const QJsonObject &response) -> QString
{
    if (const QString message = response.value(QStringLiteral("message")).toString(); !message.isEmpty())
    {
        return message;
    }

    if (response.isEmpty())
    {
        return {};
    }

    return QString::fromUtf8(QJsonDocument(response).toJson(QJsonDocument::Compact));
}

} // namespace

//---------------------------------------------------------------------------------------------------------------------
DialogLogin::DialogLogin(QWidget *parent)
  : QDialog(parent),
    m_signInButton(nullptr),
    m_emailLineEdit(new QLineEdit(this)),
    m_passwordLineEdit(new QLineEdit(this)),
    m_errorLabel(new QLabel(this))
{
    setWindowTitle(tr("Login"));
    setModal(true);
    resize(400, 0);

    auto *infoLabel = new QLabel(tr("Enter your email and password to continue."), this);
    infoLabel->setWordWrap(true);

    auto *licenseLabel =
        new QLabel(tr("Product license can be purchased at <a href=\"http://www.yourdomain.com\">"
                      "http://www.yourdomain.com</a>."),
                   this);
    licenseLabel->setOpenExternalLinks(true);
    licenseLabel->setWordWrap(true);

    m_emailLineEdit->setPlaceholderText(tr("Email"));
    m_passwordLineEdit->setPlaceholderText(tr("Password"));
    m_passwordLineEdit->setEchoMode(QLineEdit::Password);

    m_errorLabel->setStyleSheet(QStringLiteral("color: #b00020;"));
    m_errorLabel->setVisible(false);
    m_errorLabel->setWordWrap(true);

    auto *formLayout = new QFormLayout();
    formLayout->addRow(tr("Email:"), m_emailLineEdit);
    formLayout->addRow(tr("Password:"), m_passwordLineEdit);

    auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    m_signInButton = buttonBox->button(QDialogButtonBox::Ok);
    m_signInButton->setText(tr("Sign in"));
    m_signInButton->setDefault(true);

    connect(buttonBox, &QDialogButtonBox::accepted, this, &DialogLogin::ValidateCredentials);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &DialogLogin::reject);

    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->addWidget(infoLabel);
    mainLayout->addWidget(licenseLabel);
    mainLayout->addLayout(formLayout);
    mainLayout->addWidget(m_errorLabel);
    mainLayout->addWidget(buttonBox);

    m_emailLineEdit->setFocus();
}

//---------------------------------------------------------------------------------------------------------------------
auto DialogLogin::EnsureAuthenticated(QWidget *parent) -> bool
{
    QString errorMessage;
    if (TryRestoreSession(errorMessage))
    {
        return true;
    }

    DialogLogin login(parent);
    if (!errorMessage.isEmpty())
    {
        login.m_errorLabel->setText(errorMessage);
        login.m_errorLabel->setVisible(true);
    }

    return login.exec() == QDialog::Accepted;
}

//---------------------------------------------------------------------------------------------------------------------
auto DialogLogin::FetchCurrentUser(AuthenticatedUser &user, QString &resultText, QString &errorMessage) -> bool
{
    if (!TryRestoreSession(errorMessage))
    {
        return false;
    }

    user = PersistedUser();
    resultText.clear();

    if (!user.IsValid() && errorMessage.isEmpty())
    {
        errorMessage = tr("Unable to load account information.");
    }

    return user.IsValid();
}

//---------------------------------------------------------------------------------------------------------------------
auto DialogLogin::LogoutCurrentUser(QString &resultText, QString &errorMessage) -> bool
{
    if (!TryRestoreSession(errorMessage))
    {
        ClearPersistedTokens();
        resultText.clear();
        errorMessage.clear();
        return true;
    }

    SendJsonRequest(kLogoutUrl, QByteArrayLiteral("POST"), AccessToken(), QJsonObject{}, errorMessage);
    resultText.clear();

    ClearPersistedTokens();
    errorMessage.clear();
    return true;
}

//---------------------------------------------------------------------------------------------------------------------
auto DialogLogin::TryRestoreSession(QString &errorMessage) -> bool
{
    QSettings settings;
    settings.beginGroup(kSettingsAuthGroup);
    const QString accessToken = settings.value(kSettingsAccessToken).toString();
    const QString refreshToken = settings.value(kSettingsRefreshToken).toString().trimmed();
    settings.endGroup();

    if (refreshToken.isEmpty())
    {
        return false;
    }

    const QDateTime now = QDateTime::currentDateTimeUtc();
    const QDateTime refreshExpiry = PersistedRefreshExpiration();
    if (refreshExpiry.isValid() && refreshExpiry <= now)
    {
        ClearPersistedTokens();
        errorMessage = tr("Your session has expired. Please sign in again.");
        return false;
    }

    const bool refreshNeedsRotation = refreshExpiry.isValid() && now.secsTo(refreshExpiry) <= kRefreshGraceSeconds;

    if (!accessToken.isEmpty() && !refreshNeedsRotation)
    {
        return true;
    }

    QString refreshError;
    const QJsonObject response = PostJson(kRefreshUrl, QJsonObject{{QStringLiteral("refresh_token"), refreshToken}},
                                          refreshError);

    const QString newAccessToken = response.value(QStringLiteral("access_token")).toString();
    const QString newRefreshToken = response.value(QStringLiteral("refresh_token")).toString();
    qint64 accessExpiresInSeconds = response.value(QStringLiteral("access_token_expires_in")).toVariant().toLongLong();
    if (accessExpiresInSeconds <= 0)
    {
        accessExpiresInSeconds = response.value(QStringLiteral("expires_in")).toVariant().toLongLong();
    }

    qint64 refreshExpiresInSeconds =
        response.value(QStringLiteral("refresh_token_expires_in")).toVariant().toLongLong();
    const QJsonObject userObject = response.value(QStringLiteral("user")).toObject();
    const AuthenticatedUser user{userObject.value(QStringLiteral("name")).toString(),
                                 userObject.value(QStringLiteral("email")).toString()};

    if (newAccessToken.isEmpty())
    {
        ClearPersistedTokens();
        errorMessage = refreshError.isEmpty() ? tr("Unable to restore your session. Please sign in again.")
                                              : refreshError;
        return false;
    }

    const QString persistedRefreshToken = newRefreshToken.isEmpty() ? refreshToken : newRefreshToken;
    if (refreshExpiresInSeconds <= 0)
    {
        if (refreshExpiry.isValid())
        {
            refreshExpiresInSeconds = now.secsTo(refreshExpiry);
        }
    }

    PersistTokens(newAccessToken, persistedRefreshToken, accessExpiresInSeconds, refreshExpiresInSeconds, user);
    return true;
}

//---------------------------------------------------------------------------------------------------------------------
auto DialogLogin::TokenExpiration(const QString &token) -> QDateTime
{
    const QStringList parts = token.split(u'.');
    if (parts.size() < 2)
    {
        return {};
    }

    QJsonParseError parseError{};
    const QJsonDocument payload = QJsonDocument::fromJson(DecodeBase64Url(parts.at(1)), &parseError);
    if (parseError.error != QJsonParseError::NoError || !payload.isObject())
    {
        return {};
    }

    const qint64 expiration = payload.object().value(QStringLiteral("exp")).toVariant().toLongLong();
    if (expiration <= 0)
    {
        return {};
    }

    return QDateTime::fromSecsSinceEpoch(expiration, QTimeZone::UTC);
}

//---------------------------------------------------------------------------------------------------------------------
auto DialogLogin::AccessToken() -> QString
{
    QSettings settings;
    settings.beginGroup(kSettingsAuthGroup);
    const QString accessToken = settings.value(kSettingsAccessToken).toString();
    settings.endGroup();
    return accessToken;
}

//---------------------------------------------------------------------------------------------------------------------
auto DialogLogin::AccessTokenExpiration() -> QDateTime
{
    QDateTime expiration = PersistedAccessExpiration();
    if (!expiration.isValid())
    {
        expiration = TokenExpiration(AccessToken());
    }

    return expiration;
}

//---------------------------------------------------------------------------------------------------------------------
auto DialogLogin::RefreshTokenExpiration() -> QDateTime
{
    return PersistedRefreshExpiration();
}

//---------------------------------------------------------------------------------------------------------------------
auto DialogLogin::PersistedAccessExpiration() -> QDateTime
{
    QSettings settings;
    settings.beginGroup(kSettingsAuthGroup);
    const QString accessTokenExpiresAt = settings.value(kSettingsAccessTokenExpiresAt).toString();
    settings.endGroup();

    return QDateTime::fromString(accessTokenExpiresAt, Qt::ISODate);
}

//---------------------------------------------------------------------------------------------------------------------
auto DialogLogin::PersistedRefreshExpiration() -> QDateTime
{
    QSettings settings;
    settings.beginGroup(kSettingsAuthGroup);
    const QString refreshTokenExpiresAt = settings.value(kSettingsRefreshTokenExpiresAt).toString();
    settings.endGroup();

    return QDateTime::fromString(refreshTokenExpiresAt, Qt::ISODate);
}

//---------------------------------------------------------------------------------------------------------------------
auto DialogLogin::PersistedUser() -> AuthenticatedUser
{
    QSettings settings;
    settings.beginGroup(kSettingsAuthGroup);
    AuthenticatedUser user{settings.value(kSettingsUserName).toString(), settings.value(kSettingsUserEmail).toString()};
    settings.endGroup();
    return user;
}

//---------------------------------------------------------------------------------------------------------------------
auto DialogLogin::PersistTokens(const QString &accessToken, const QString &refreshToken, qint64 accessExpiresInSeconds,
                                qint64 refreshExpiresInSeconds, const AuthenticatedUser &user) -> void
{
    QSettings settings;
    settings.beginGroup(kSettingsAuthGroup);
    settings.setValue(kSettingsAccessToken, accessToken);
    settings.setValue(kSettingsRefreshToken, refreshToken);
    settings.setValue(kSettingsUserName, user.name);
    settings.setValue(kSettingsUserEmail, user.email);
    if (accessExpiresInSeconds > 0)
    {
        settings.setValue(kSettingsAccessTokenExpiresAt,
                          QDateTime::currentDateTimeUtc().addSecs(accessExpiresInSeconds).toString(Qt::ISODate));
    }
    else
    {
        settings.remove(kSettingsAccessTokenExpiresAt);
    }

    if (refreshExpiresInSeconds > 0)
    {
        settings.setValue(kSettingsRefreshTokenExpiresAt,
                          QDateTime::currentDateTimeUtc().addSecs(refreshExpiresInSeconds).toString(Qt::ISODate));
    }
    else
    {
        settings.remove(kSettingsRefreshTokenExpiresAt);
    }

    settings.endGroup();
    settings.sync();
}

//---------------------------------------------------------------------------------------------------------------------
auto DialogLogin::ClearPersistedTokens() -> void
{
    QSettings settings;
    settings.beginGroup(kSettingsAuthGroup);
    settings.remove(QString());
    settings.endGroup();
    settings.sync();
}

//---------------------------------------------------------------------------------------------------------------------
void DialogLogin::ValidateCredentials()
{
    const QString email = m_emailLineEdit->text().trimmed();
    const QString password = m_passwordLineEdit->text();

    if (email.isEmpty() || password.isEmpty())
    {
        m_errorLabel->setText(tr("Enter your email and password."));
        m_errorLabel->setVisible(true);
        return;
    }

    QString errorMessage;
    m_signInButton->setEnabled(false);
    m_signInButton->setText(tr("Connecting..."));
    m_errorLabel->setStyleSheet(QString());
    m_errorLabel->setText(tr("Trying to connect..."));
    m_errorLabel->setVisible(true);
    QCoreApplication::processEvents();

    const QJsonObject response =
        PostJson(kLoginUrl,
                 QJsonObject{{QStringLiteral("email"), email}, {QStringLiteral("password"), password}},
                 errorMessage);
    if (!response.isEmpty())
    {
        errorMessage.clear();
    }

    const QString accessToken = response.value(QStringLiteral("access_token")).toString();
    const QString refreshToken = response.value(QStringLiteral("refresh_token")).toString();
    qint64 accessExpiresInSeconds = response.value(QStringLiteral("access_token_expires_in")).toVariant().toLongLong();
    if (accessExpiresInSeconds <= 0)
    {
        accessExpiresInSeconds = response.value(QStringLiteral("expires_in")).toVariant().toLongLong();
    }

    const qint64 refreshExpiresInSeconds =
        response.value(QStringLiteral("refresh_token_expires_in")).toVariant().toLongLong();
    const QJsonObject userObject = response.value(QStringLiteral("user")).toObject();
    const AuthenticatedUser user{userObject.value(QStringLiteral("name")).toString(),
                                 userObject.value(QStringLiteral("email")).toString()};
    const QString resultText = ResponseText(response);

    if (accessToken.isEmpty())
    {
        m_signInButton->setEnabled(true);
        m_signInButton->setText(tr("Sign in"));
        m_errorLabel->setStyleSheet(QStringLiteral("color: #b00020;"));
        m_errorLabel->setText(tr("Invalid credentials."));
        m_errorLabel->setVisible(true);
        QMessageBox::warning(this, tr("Attention"), tr("Invalid credentials."));
        m_passwordLineEdit->clear();
        m_passwordLineEdit->setFocus();
        return;
    }

    if (refreshToken.isEmpty())
    {
        m_signInButton->setEnabled(true);
        m_signInButton->setText(tr("Sign in"));
        m_errorLabel->setStyleSheet(QStringLiteral("color: #b00020;"));
        m_errorLabel->setText(tr("No paid license was found for this account. Purchase your license at "
                                 "http://www.yourdomain.com."));
        m_errorLabel->setVisible(true);
        return;
    }

    PersistTokens(accessToken, refreshToken, accessExpiresInSeconds, refreshExpiresInSeconds, user);
    QMessageBox::information(this,
                             tr("Login"),
                             tr("Login successful. Thank you for using Moda Maker."));
    accept();
}
