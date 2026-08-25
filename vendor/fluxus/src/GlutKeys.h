// fluxus->JUCE minimal port: GLUT key + modifier constants only.
// GLEditor needs these values (from freeglut) but no GLUT library — the JUCE
// host maps its key events to these codes and calls GLEditor::Handle.
#ifndef FLUXUS_GLUT_KEYS_H
#define FLUXUS_GLUT_KEYS_H

#ifndef GLUT_KEY_F1
#define GLUT_KEY_F1        1
#define GLUT_KEY_F2        2
#define GLUT_KEY_F3        3
#define GLUT_KEY_F4        4
#define GLUT_KEY_F5        5
#define GLUT_KEY_F6        6
#define GLUT_KEY_F7        7
#define GLUT_KEY_F8        8
#define GLUT_KEY_F9        9
#define GLUT_KEY_F10       10
#define GLUT_KEY_F11       11
#define GLUT_KEY_F12       12
#define GLUT_KEY_LEFT      100
#define GLUT_KEY_UP        101
#define GLUT_KEY_RIGHT     102
#define GLUT_KEY_DOWN      103
#define GLUT_KEY_PAGE_UP   104
#define GLUT_KEY_PAGE_DOWN 105
#define GLUT_KEY_HOME      106
#define GLUT_KEY_END       107
#define GLUT_KEY_INSERT    108
#endif

#ifndef GLUT_ACTIVE_SHIFT
#define GLUT_ACTIVE_SHIFT  1
#define GLUT_ACTIVE_CTRL   2
#define GLUT_ACTIVE_ALT    4
#endif

#endif
