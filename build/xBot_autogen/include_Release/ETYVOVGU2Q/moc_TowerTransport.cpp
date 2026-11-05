/****************************************************************************
** Meta object code from reading C++ file 'TowerTransport.h'
**
** Created by: The Qt Meta Object Compiler version 69 (Qt 6.10.3)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../../src/bot/TowerTransport.h"
#include <QtNetwork/QSslError>
#include <QtCore/qmetatype.h>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'TowerTransport.h' doesn't include <QObject>."
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
struct qt_meta_tag_ZN14TowerTransportE_t {};
} // unnamed namespace

template <> constexpr inline auto TowerTransport::qt_create_metaobjectdata<qt_meta_tag_ZN14TowerTransportE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "TowerTransport",
        "balancesUpdated",
        "",
        "QMap<QString,double>",
        "balances",
        "balancesFailed",
        "error"
    };

    QtMocHelpers::UintData qt_methods {
        // Signal 'balancesUpdated'
        QtMocHelpers::SignalData<void(const QMap<QString,double> &)>(1, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 3, 4 },
        }}),
        // Signal 'balancesFailed'
        QtMocHelpers::SignalData<void(const QString &)>(5, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 6 },
        }}),
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<TowerTransport, qt_meta_tag_ZN14TowerTransportE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject TowerTransport::staticMetaObject = { {
    QMetaObject::SuperData::link<IStakeTransport::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN14TowerTransportE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN14TowerTransportE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN14TowerTransportE_t>.metaTypes,
    nullptr
} };

void TowerTransport::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<TowerTransport *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->balancesUpdated((*reinterpret_cast<std::add_pointer_t<QMap<QString,double>>>(_a[1]))); break;
        case 1: _t->balancesFailed((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        default: ;
        }
    }
    if (_c == QMetaObject::IndexOfMethod) {
        if (QtMocHelpers::indexOfMethod<void (TowerTransport::*)(const QMap<QString,double> & )>(_a, &TowerTransport::balancesUpdated, 0))
            return;
        if (QtMocHelpers::indexOfMethod<void (TowerTransport::*)(const QString & )>(_a, &TowerTransport::balancesFailed, 1))
            return;
    }
}

const QMetaObject *TowerTransport::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *TowerTransport::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN14TowerTransportE_t>.strings))
        return static_cast<void*>(this);
    return IStakeTransport::qt_metacast(_clname);
}

int TowerTransport::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = IStakeTransport::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 2)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 2;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 2)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 2;
    }
    return _id;
}

// SIGNAL 0
void TowerTransport::balancesUpdated(const QMap<QString,double> & _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 0, nullptr, _t1);
}

// SIGNAL 1
void TowerTransport::balancesFailed(const QString & _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 1, nullptr, _t1);
}
QT_WARNING_POP
