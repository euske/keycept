# Makefile

DEL=del /f
COPY=copy /y
MT=mt -nologo
CL=cl /nologo
RC=rc
LINK=link /nologo
ZIP=zip

CFLAGS=/MD /O2 /GA /Zi
LDFLAGS=/DEBUG /OPT:REF /OPT:ICF
RCFLAGS=
DEFS_COMMON=/D WIN32 /D UNICODE /D _UNICODE
DEFS_CONSOLE=$(DEFS_COMMON) /D CONSOLE /D _CONSOLE
DEFS_WINDOWS=$(DEFS_COMMON) /D WINDOWS /D _WINDOWS
DEFS=$(DEFS_CONSOLE)
LIBS=
INCLUDES=
TARGETS=KeyCept.exe Hookey.dll
DESTDIR=%UserProfile%\bin

all: $(TARGETS)

install: clean
	$(MAKE) $(TARGETS) DEFS="$(DEFS_WINDOWS)"
	$(COPY) KeyCept.exe $(DESTDIR)
	$(COPY) Hookey.dll $(DESTDIR)

test: $(TARGETS)
	.\KeyCept.exe

clean:
	-$(DEL) KeyCept.zip
	-$(DEL) $(TARGETS)
	-$(DEL) *.lib *.exp *.obj *.res *.ilk *.pdb *.manifest

pack: KeyCept.zip

KeyCept.zip: $(TARGETS) README.md
	$(ZIP) $@ $(TARGETS) README.md

KeyCept.exe: KeyCept.obj ini.obj KeyCept.res
	$(LINK) $(LDFLAGS) /manifest /out:$@ $** $(LIBS)
	$(MT) -manifest $@.manifest -outputresource:$@;1

Hookey.dll: Hookey.obj
	$(LINK) /DLL $(LDFLAGS) /manifest /out:$@ $** $(LIBS)
	$(MT) -manifest $@.manifest -outputresource:$@;1

Hookey.obj: Hookey.cpp
	$(CL) /LD $(CFLAGS) /Fo$@ /c $** $(DEFS) $(INCLUDES)

KeyCept.cpp: Resource.h Hookey.h
KeyCept.rc: Resource.h KeyCeptOn.ico KeyCeptOff.ico
Hookey.cpp: Hookey.h
ini.c: ini.h

.cpp.obj:
	$(CL) $(CFLAGS) /Fo$@ /c $< $(DEFS) $(INCLUDES)
.rc.res:
	$(RC) $(RCFLAGS) $<
