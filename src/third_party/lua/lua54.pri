# qmake users: add   include(third_party/lua/lua54.pri)   to your .pro file.
INCLUDEPATH += $$PWD
SOURCES += $$files($$PWD/*.c)
win32 {
    # nothing extra needed
} else {
    DEFINES += LUA_USE_POSIX
}
