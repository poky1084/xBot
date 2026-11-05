/****************************************************************************
** Meta object code from reading C++ file 'ManualBetPanel.h'
**
** Created by: The Qt Meta Object Compiler version 69 (Qt 6.10.3)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../../src/ui/ManualBetPanel.h"
#include <QtCore/qmetatype.h>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'ManualBetPanel.h' doesn't include <QObject>."
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
struct qt_meta_tag_ZN14ManualBetPanelE_t {};
} // unnamed namespace

template <> constexpr inline auto ManualBetPanel::qt_create_metaobjectdata<qt_meta_tag_ZN14ManualBetPanelE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "ManualBetPanel",
        "rollRequested",
        "",
        "hiloMoveRequested",
        "move",
        "blackjackMoveRequested",
        "gameChanged",
        "amountChanged",
        "amount",
        "contentHeightChanged",
        "onBetResult",
        "BetRecord",
        "record",
        "updateBalance",
        "balance",
        "showHiloRound",
        "QJsonObject",
        "bet",
        "active",
        "playable",
        "showBlackjackRound",
        "onTargetSlider",
        "value",
        "onSideToggled",
        "onMultiplierEdited",
        "onChanceEdited",
        "halfAmount",
        "doubleAmount"
    };

    QtMocHelpers::UintData qt_methods {
        // Signal 'rollRequested'
        QtMocHelpers::SignalData<void()>(1, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'hiloMoveRequested'
        QtMocHelpers::SignalData<void(const QString &)>(3, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 4 },
        }}),
        // Signal 'blackjackMoveRequested'
        QtMocHelpers::SignalData<void(const QString &)>(5, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 4 },
        }}),
        // Signal 'gameChanged'
        QtMocHelpers::SignalData<void()>(6, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'amountChanged'
        QtMocHelpers::SignalData<void(double)>(7, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Double, 8 },
        }}),
        // Signal 'contentHeightChanged'
        QtMocHelpers::SignalData<void()>(9, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'onBetResult'
        QtMocHelpers::SlotData<void(const BetRecord &)>(10, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 11, 12 },
        }}),
        // Slot 'updateBalance'
        QtMocHelpers::SlotData<void(double)>(13, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Double, 14 },
        }}),
        // Slot 'showHiloRound'
        QtMocHelpers::SlotData<void(const QJsonObject &, bool, bool)>(15, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 16, 17 }, { QMetaType::Bool, 18 }, { QMetaType::Bool, 19 },
        }}),
        // Slot 'showHiloRound'
        QtMocHelpers::SlotData<void(const QJsonObject &, bool)>(15, 2, QMC::AccessPublic | QMC::MethodCloned, QMetaType::Void, {{
            { 0x80000000 | 16, 17 }, { QMetaType::Bool, 18 },
        }}),
        // Slot 'showBlackjackRound'
        QtMocHelpers::SlotData<void(const QJsonObject &, bool, bool)>(20, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 16, 17 }, { QMetaType::Bool, 18 }, { QMetaType::Bool, 19 },
        }}),
        // Slot 'showBlackjackRound'
        QtMocHelpers::SlotData<void(const QJsonObject &, bool)>(20, 2, QMC::AccessPublic | QMC::MethodCloned, QMetaType::Void, {{
            { 0x80000000 | 16, 17 }, { QMetaType::Bool, 18 },
        }}),
        // Slot 'onTargetSlider'
        QtMocHelpers::SlotData<void(int)>(21, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { QMetaType::Int, 22 },
        }}),
        // Slot 'onSideToggled'
        QtMocHelpers::SlotData<void()>(23, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onMultiplierEdited'
        QtMocHelpers::SlotData<void()>(24, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onChanceEdited'
        QtMocHelpers::SlotData<void()>(25, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'halfAmount'
        QtMocHelpers::SlotData<void()>(26, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'doubleAmount'
        QtMocHelpers::SlotData<void()>(27, 2, QMC::AccessPrivate, QMetaType::Void),
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<ManualBetPanel, qt_meta_tag_ZN14ManualBetPanelE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject ManualBetPanel::staticMetaObject = { {
    QMetaObject::SuperData::link<QWidget::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN14ManualBetPanelE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN14ManualBetPanelE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN14ManualBetPanelE_t>.metaTypes,
    nullptr
} };

void ManualBetPanel::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<ManualBetPanel *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->rollRequested(); break;
        case 1: _t->hiloMoveRequested((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        case 2: _t->blackjackMoveRequested((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        case 3: _t->gameChanged(); break;
        case 4: _t->amountChanged((*reinterpret_cast<std::add_pointer_t<double>>(_a[1]))); break;
        case 5: _t->contentHeightChanged(); break;
        case 6: _t->onBetResult((*reinterpret_cast<std::add_pointer_t<BetRecord>>(_a[1]))); break;
        case 7: _t->updateBalance((*reinterpret_cast<std::add_pointer_t<double>>(_a[1]))); break;
        case 8: _t->showHiloRound((*reinterpret_cast<std::add_pointer_t<QJsonObject>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<bool>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<bool>>(_a[3]))); break;
        case 9: _t->showHiloRound((*reinterpret_cast<std::add_pointer_t<QJsonObject>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<bool>>(_a[2]))); break;
        case 10: _t->showBlackjackRound((*reinterpret_cast<std::add_pointer_t<QJsonObject>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<bool>>(_a[2])),(*reinterpret_cast<std::add_pointer_t<bool>>(_a[3]))); break;
        case 11: _t->showBlackjackRound((*reinterpret_cast<std::add_pointer_t<QJsonObject>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<bool>>(_a[2]))); break;
        case 12: _t->onTargetSlider((*reinterpret_cast<std::add_pointer_t<int>>(_a[1]))); break;
        case 13: _t->onSideToggled(); break;
        case 14: _t->onMultiplierEdited(); break;
        case 15: _t->onChanceEdited(); break;
        case 16: _t->halfAmount(); break;
        case 17: _t->doubleAmount(); break;
        default: ;
        }
    }
    if (_c == QMetaObject::IndexOfMethod) {
        if (QtMocHelpers::indexOfMethod<void (ManualBetPanel::*)()>(_a, &ManualBetPanel::rollRequested, 0))
            return;
        if (QtMocHelpers::indexOfMethod<void (ManualBetPanel::*)(const QString & )>(_a, &ManualBetPanel::hiloMoveRequested, 1))
            return;
        if (QtMocHelpers::indexOfMethod<void (ManualBetPanel::*)(const QString & )>(_a, &ManualBetPanel::blackjackMoveRequested, 2))
            return;
        if (QtMocHelpers::indexOfMethod<void (ManualBetPanel::*)()>(_a, &ManualBetPanel::gameChanged, 3))
            return;
        if (QtMocHelpers::indexOfMethod<void (ManualBetPanel::*)(double )>(_a, &ManualBetPanel::amountChanged, 4))
            return;
        if (QtMocHelpers::indexOfMethod<void (ManualBetPanel::*)()>(_a, &ManualBetPanel::contentHeightChanged, 5))
            return;
    }
}

const QMetaObject *ManualBetPanel::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *ManualBetPanel::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN14ManualBetPanelE_t>.strings))
        return static_cast<void*>(this);
    return QWidget::qt_metacast(_clname);
}

int ManualBetPanel::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QWidget::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 18)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 18;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 18)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 18;
    }
    return _id;
}

// SIGNAL 0
void ManualBetPanel::rollRequested()
{
    QMetaObject::activate(this, &staticMetaObject, 0, nullptr);
}

// SIGNAL 1
void ManualBetPanel::hiloMoveRequested(const QString & _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 1, nullptr, _t1);
}

// SIGNAL 2
void ManualBetPanel::blackjackMoveRequested(const QString & _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 2, nullptr, _t1);
}

// SIGNAL 3
void ManualBetPanel::gameChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 3, nullptr);
}

// SIGNAL 4
void ManualBetPanel::amountChanged(double _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 4, nullptr, _t1);
}

// SIGNAL 5
void ManualBetPanel::contentHeightChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 5, nullptr);
}
QT_WARNING_POP
