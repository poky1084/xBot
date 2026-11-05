/****************************************************************************
** Meta object code from reading C++ file 'StakeClient.h'
**
** Created by: The Qt Meta Object Compiler version 69 (Qt 6.10.3)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../../src/bot/StakeClient.h"
#include <QtNetwork/QSslError>
#include <QtCore/qmetatype.h>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'StakeClient.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 69
#error "This file was generated using the moc from 6.10.3. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

#ifndef Q_CONSTINIT
#define Q_CONSTINIT
#endif

QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
QT_WARNING_DISABLE_GCC("-Wuseless-cast")
namespace {
struct qt_meta_tag_ZN11StakeClientE_t {};
} // unnamed namespace

template <> constexpr inline auto StakeClient::qt_create_metaobjectdata<qt_meta_tag_ZN11StakeClientE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "StakeClient",
        "balancesReady",
        "",
        "QMap<QString,double>",
        "balances",
        "balancesFailed",
        "error",
        "backgroundBalancesReady",
        "backgroundBalancesFailed",
        "diceFinished",
        "LiveBetResult",
        "result",
        "sessionLost",
        "reason",
        "seedRotated",
        "clientSeed",
        "nonce",
        "seedHash",
        "nextSeedHash",
        "seedRotateFailed",
        "vaultDeposited",
        "amount",
        "currency",
        "vaultFailed"
    };

    QtMocHelpers::UintData qt_methods {
        // Signal 'balancesReady'
        QtMocHelpers::SignalData<void(const QMap<QString,double> &)>(1, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 3, 4 },
        }}),
        // Signal 'balancesFailed'
        QtMocHelpers::SignalData<void(const QString &)>(5, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 6 },
        }}),
        // Signal 'backgroundBalancesReady'
        QtMocHelpers::SignalData<void(const QMap<QString,double> &)>(7, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 3, 4 },
        }}),
        // Signal 'backgroundBalancesFailed'
        QtMocHelpers::SignalData<void(const QString &)>(8, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 6 },
        }}),
        // Signal 'diceFinished'
        QtMocHelpers::SignalData<void(const LiveBetResult &)>(9, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 10, 11 },
        }}),
        // Signal 'sessionLost'
        QtMocHelpers::SignalData<void(const QString &)>(12, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 13 },
        }}),
        // Signal 'seedRotated'
        QtMocHelpers::SignalData<void(const QString &, qint64, const QString &, const QString &)>(14, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 15 }, { QMetaType::LongLong, 16 }, { QMetaType::QString, 17 }, { QMetaType::QString, 18 },
        }}),
        // Signal 'seedRotateFailed'
        QtMocHelpers::SignalData<void(const QString &)>(19, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 6 },
        }}),
        // Signal 'vaultDeposited'
        QtMocHelpers::SignalData<void(double, const QString &)>(20, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Double, 21 }, { QMetaType::QString, 22 },
        }}),
        // Signal 'vaultFailed'
        QtMocHelpers::SignalData<void(const QString &)>(23, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 6 },
        }}),
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<StakeClient, qt_meta_tag_ZN11StakeClientE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject StakeClient::staticMetaObject = { {
    QMetaObject::SuperData::link<QObject::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN11StakeClientE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN11StakeClientE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN11StakeClientE_t>.metaTypes,
    nullptr
} };

void StakeClient::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<StakeClient *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->balancesReady((*reinterpret_cast<std::add_pointer_t<QMap<QString,double>>>(_a[1]))); break;
        case 1: _t->balancesFailed((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        case 2: _t->backgroundBalancesReady((*reinterpret_cast<std::add_pointer_t<QMap<QString,double>>>(_a[1]))); break;
        case 3: _t->backgroundBalancesFailed((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        case 4: _t->diceFinished((*reinterpret_cast<std::add_pointer_t<LiveBetResult>>(_a[1]))); break;
        case 5: _t->sessionLost((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        case 6: _t->seedRotated((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<qint64>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[3])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[4]))); break;
        case 7: _t->seedRotateFailed((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        case 8: _t->vaultDeposited((*reinterpret_cast<std::add_pointer_t<double>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<QString>>(_a[2]))); break;
        case 9: _t->vaultFailed((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        default: ;
        }
    }
    if (_c == QMetaObject::IndexOfMethod) {
        if (QtMocHelpers::indexOfMethod<void (StakeClient::*)(const QMap<QString,double> & )>(_a, &StakeClient::balancesReady, 0))
            return;
        if (QtMocHelpers::indexOfMethod<void (StakeClient::*)(const QString & )>(_a, &StakeClient::balancesFailed, 1))
            return;
        if (QtMocHelpers::indexOfMethod<void (StakeClient::*)(const QMap<QString,double> & )>(_a, &StakeClient::backgroundBalancesReady, 2))
            return;
        if (QtMocHelpers::indexOfMethod<void (StakeClient::*)(const QString & )>(_a, &StakeClient::backgroundBalancesFailed, 3))
            return;
        if (QtMocHelpers::indexOfMethod<void (StakeClient::*)(const LiveBetResult & )>(_a, &StakeClient::diceFinished, 4))
            return;
        if (QtMocHelpers::indexOfMethod<void (StakeClient::*)(const QString & )>(_a, &StakeClient::sessionLost, 5))
            return;
        if (QtMocHelpers::indexOfMethod<void (StakeClient::*)(const QString & , qint64 , const QString & , const QString & )>(_a, &StakeClient::seedRotated, 6))
            return;
        if (QtMocHelpers::indexOfMethod<void (StakeClient::*)(const QString & )>(_a, &StakeClient::seedRotateFailed, 7))
            return;
        if (QtMocHelpers::indexOfMethod<void (StakeClient::*)(double , const QString & )>(_a, &StakeClient::vaultDeposited, 8))
            return;
        if (QtMocHelpers::indexOfMethod<void (StakeClient::*)(const QString & )>(_a, &StakeClient::vaultFailed, 9))
            return;
    }
}

const QMetaObject *StakeClient::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *StakeClient::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN11StakeClientE_t>.strings))
        return static_cast<void*>(this);
    return QObject::qt_metacast(_clname);
}

int StakeClient::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QObject::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 10)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 10;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 10)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 10;
    }
    return _id;
}

// SIGNAL 0
void StakeClient::balancesReady(const QMap<QString,double> & _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 0, nullptr, _t1);
}

// SIGNAL 1
void StakeClient::balancesFailed(const QString & _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 1, nullptr, _t1);
}

// SIGNAL 2
void StakeClient::backgroundBalancesReady(const QMap<QString,double> & _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 2, nullptr, _t1);
}

// SIGNAL 3
void StakeClient::backgroundBalancesFailed(const QString & _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 3, nullptr, _t1);
}

// SIGNAL 4
void StakeClient::diceFinished(const LiveBetResult & _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 4, nullptr, _t1);
}

// SIGNAL 5
void StakeClient::sessionLost(const QString & _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 5, nullptr, _t1);
}

// SIGNAL 6
void StakeClient::seedRotated(const QString & _t1, qint64 _t2, const QString & _t3, const QString & _t4)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 6, nullptr, _t1, _t2, _t3, _t4);
}

// SIGNAL 7
void StakeClient::seedRotateFailed(const QString & _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 7, nullptr, _t1);
}

// SIGNAL 8
void StakeClient::vaultDeposited(double _t1, const QString & _t2)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 8, nullptr, _t1, _t2);
}

// SIGNAL 9
void StakeClient::vaultFailed(const QString & _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 9, nullptr, _t1);
}
QT_WARNING_POP
