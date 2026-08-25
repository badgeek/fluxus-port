// fluxus->JUCE minimal port: global current-backend, defaulting to GLBackend.
#include "RenderBackend.h"
#include "GLBackend.h"

namespace Fluxus {

static GLBackend        s_defaultGL;
static IRenderBackend*  s_backend = &s_defaultGL;

IRenderBackend* Backend()                  { return s_backend; }
void            SetBackend(IRenderBackend* b) { s_backend = b ? b : &s_defaultGL; }

} // namespace Fluxus
