#pragma once
#include "iup.h"
#include "core/controller.h"
Ihandle *sequenceUICreate(AppController *app, void (*perform)(AppAction), void (*changed)(void));
Ihandle *sequenceUIStatus(void);
void sequenceUIInitialize(void);
void sequenceUIRefresh(void);
void sequenceUISetAdvanced(bool advanced);
