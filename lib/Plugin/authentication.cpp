/*
 * Copyright (C) 2013 Daniel Nicoletti <dantti12@gmail.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * along with this library; see the file COPYING.LIB. If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#include "authentication_p.h"

#include "cutelyst.h"
#include "session.h"

#include <QDebug>

using namespace CutelystPlugin;

Authentication::Authentication(QObject *parent) :
    Plugin(parent),
    d_ptr(new AuthenticationPrivate)
{
}

Authentication::~Authentication()
{
    delete d_ptr;
}

void Authentication::addRealm(Authentication::Realm *realm)
{
    addRealm(QLatin1String("default"), realm);
}

void Authentication::addRealm(const QString &name, Authentication::Realm *realm, bool defaultRealm)
{
    Q_D(Authentication);
    if (defaultRealm) {
        d->defaultRealm = name;
    }
    d->realms.insert(name, realm);
    d->realmsOrder.append(name);
    realm->m_autehntication = this;
    realm->m_name = name;
}

void Authentication::setUseSession(bool use)
{

}

bool Authentication::useSession() const
{

}

Authentication::User Authentication::authenticate(Cutelyst *c, const QString &username, const QString &password, const QString &realm)
{
    CStringHash userinfo;
    userinfo.insert(QLatin1String("username"), username);
    userinfo.insert(QLatin1String("password"), password);
    return authenticate(c, userinfo, realm);
}

Authentication::User Authentication::authenticate(Cutelyst *c, const CStringHash &userinfo, const QString &realm)
{
    Q_D(Authentication);
    qDebug() << Q_FUNC_INFO << c << realm << userinfo;

    Authentication::Realm *realmPtr = d->realm(realm);
    qDebug() << Q_FUNC_INFO << realmPtr;

    if (realmPtr) {
        return realmPtr->authenticate(c, userinfo);
    }

    qWarning() << Q_FUNC_INFO << "Could not find realm" << realm;
    return User();
}

Authentication::User Authentication::findUser(Cutelyst *c, const CStringHash &userinfo, const QString &realm)
{
    Q_D(Authentication);

    Authentication::Realm *realmPtr = d->realm(realm);
    if (realmPtr) {
        return realmPtr->findUser(c, userinfo);
    }

    qWarning() << Q_FUNC_INFO << "Could not find realm" << realm;
    return User();
}

Authentication::User Authentication::user(Cutelyst *c)
{
    QVariant user = pluginProperty(c, "user");
    if (user.isNull()) {
        return restoreUser(c, User(), QString());
    }
    return user.value<User>();
}

bool Authentication::userExists(Cutelyst *c)
{
    return !user(c).isNull();
}

bool Authentication::userInRealm(Cutelyst *c, const QString &realm)
{
    QVariant user = pluginProperty(c, "user");
    if (user.isNull()) {
        return !restoreUser(c, User(), realm).isNull();
    }
    return false;
}

void Authentication::logout(Cutelyst *c)
{

}

void Authentication::setAuthenticated(Cutelyst *c, const User &user, const QString &realmName)
{
    Q_D(Authentication);

    qDebug() << Q_FUNC_INFO << user.id();

    setPluginProperty(c, "user", qVariantFromValue(user));

    qDebug() << Q_FUNC_INFO << "Set plugin value";


    Authentication::Realm *realmPtr = d->realm(realmName);
    qDebug() << Q_FUNC_INFO << "realm" << realmPtr;

    if (!realmPtr) {
        qWarning() << Q_FUNC_INFO << "Called with invalid realm" << realmName;
    }
    // TODO implement a user class
//    $user->auth_realm($realm->name);

    persistUser(c, user, realmName);
    qDebug() << Q_FUNC_INFO << user.id();
}

void Authentication::persistUser(Cutelyst *c, const User &user, const QString &realmName)
{
    Q_D(Authentication);
    qDebug() << Q_FUNC_INFO << "persisting" << user;

    if (userExists(c)) {
        qDebug() << Q_FUNC_INFO << "persisting1" << user;

        Session *session = c->plugin<Session*>();
        if (session && session->isValid(c)) {
            session->setValue(c, "Authentication::userRealm", realmName);
        }
        qDebug() << Q_FUNC_INFO << "persisting2" << user;

        Authentication::Realm *realmPtr = d->realm(realmName);
        if (realmPtr) {
            realmPtr->persistUser(c, user);
        }
        qDebug() << Q_FUNC_INFO << "persisting3" << user;

    }
}

Authentication::User Authentication::restoreUser(Cutelyst *c, const User &frozenUser, const QString &realmName)
{
    Q_D(Authentication);

    Authentication::Realm *realmPtr;
    if (!realmName.isNull()) {
//        c = d->realm(realmName);
    } else {
        realmPtr = findRealmForPersistedUser(c);
    }

    User user;
    if (realmPtr) {
        return realmPtr->restoreUser(c, frozenUser);
    }

    // TODO
    // $user->auth_realm($realm->name) if $user;

    return user;
}

Authentication::Realm *Authentication::findRealmForPersistedUser(Cutelyst *c)
{
    Q_D(Authentication);

    Authentication::Realm *realm;

    Session *session = c->plugin<Session*>();
    if (session &&
            session->isValid(c) &&
            !session->value(c, "Authentication::userRealm").isNull()) {
        QString realmName = session->value(c, "Authentication::userRealm").toString();
        realm = d->realms.value(realmName);
        if (realm && !realm->userIsRestorable(c).isNull()) {
            return realm;
        }
    } else {
        // we have no choice but to ask each realm whether it has a persisted user.
        foreach (const QString &realmName, d->realmsOrder) {
            Authentication::Realm *realm = d->realms.value(realmName);
            if (realm && !realm->userIsRestorable(c).isNull()) {
                return realm;
            }
        }
    }

    return 0;
}

Authentication::Realm::Realm(Authentication::Store *store, Authentication::Credential *credential) :
    m_store(store),
    m_credential(credential)
{

}

Authentication::User Authentication::Realm::findUser(Cutelyst *c, const CStringHash &userinfo)
{
    User ret = m_store->findUser(c, userinfo);

    if (ret.isNull()) {
        if (m_store->canAutoCreateUser()) {
            ret = m_store->autoCreateUser(c, userinfo);
        }
    } else if (m_store->canAutoUpdateUser()) {
        ret = m_store->autoUpdateUser(c, userinfo);
    }

    return ret;
}

Authentication::User Authentication::Realm::authenticate(Cutelyst *c, const CStringHash &authinfo)
{
    qDebug() << Q_FUNC_INFO << m_credential;
    User user = m_credential->authenticate(c, this, authinfo);
    qDebug() << Q_FUNC_INFO << user.id();
    if (!user.isNull()) {
        c->plugin<Authentication*>()->setAuthenticated(c, user, m_name);
    }
    qDebug() << Q_FUNC_INFO << user.id();

    return user;
}

Authentication::User Authentication::Realm::persistUser(Cutelyst *c, const Authentication::User &user)
{
    Session *session = c->plugin<Session*>();
    if (session && session->isValid(c)) {
        QVariant value;
        if (m_store->canForSession()) {
            value = m_store->forSession(c, user);
        } else {
            value = user.forSession(c);
        }
        session->setValue(c, "Authentication::user", value);
    }

    return user;
}

Authentication::User Authentication::Realm::restoreUser(Cutelyst *c, const User &frozenUser)
{
    User user = frozenUser;
    if (user.isNull()) {
        user = userIsRestorable(c);
    }

    // TODO restore a User object
    Session *session = c->plugin<Session*>();
    if (session && session->isValid(c)) {
//        QVariant value;
//        if (m_store->canForSession()) {
//            value = m_store->forSession(c, user);
//        } else {
//            value = user.forSession(c);
//        }
        return session->value(c, "Authentication::user").value<User>();
    }

    return user;
}

Authentication::User Authentication::Realm::userIsRestorable(Cutelyst *c)
{
    Session *session = c->plugin<Session*>();
    if (session && session->isValid(c)) {
        return session->value(c, "Authentication::user").value<User>();
    }
    return User();
}

Authentication::Realm *AuthenticationPrivate::realm(const QString &realmName) const
{
    QString name = realmName;
    if (name.isNull()) {
        name = defaultRealm;
    }
    return realms.value(name);
}


bool Authentication::Store::canAutoCreateUser() const
{
    return false;
}

Authentication::User Authentication::Store::autoCreateUser(Cutelyst *c, const CStringHash &userinfo) const
{
    return User();
}

bool Authentication::Store::canAutoUpdateUser() const
{
    return false;
}

Authentication::User Authentication::Store::autoUpdateUser(Cutelyst *c, const CStringHash &userinfo) const
{
    return User();
}

bool Authentication::Store::canForSession() const
{
    return false;
}

QVariant Authentication::Store::forSession(Cutelyst *c, const User &user)
{
    return QVariant();
}


Authentication::User::User()
{

}

Authentication::User::User(const QString &id) :
    m_id(id)
{

}

QString Authentication::User::id() const
{
    return m_id;
}

bool Authentication::User::isNull() const
{
    return m_id.isNull();
}

QVariant Authentication::User::forSession(Cutelyst *c) const
{
    return QVariant();
}

void Authentication::User::fromSession(Cutelyst *c)
{

}
