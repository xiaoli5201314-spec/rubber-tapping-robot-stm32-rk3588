#ifndef STARTUP_SAFETY_H
#define STARTUP_SAFETY_H

typedef void (*StartupSafetyFatalHook)(void);

/* Establish a handle-independent hardware brake, then enter the supplied
   fatal path. The hook permits behavior-level failure injection in host tests. */
void StartupSafety_EnterFatal(StartupSafetyFatalHook fatal_hook);

#endif /* STARTUP_SAFETY_H */
