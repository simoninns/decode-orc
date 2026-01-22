#ifndef ORC_PUBLIC_TYPES_H
#define ORC_PUBLIC_TYPES_H

#include <field_id.h>
#include <node_id.h>
#include <node_type.h>
#include <error_codes.h>

// Opaque handles for GUI/CLI
#ifdef __cplusplus
extern "C" {
#endif

typedef struct OrcProject* OrcProjectHandle;
typedef struct OrcRenderer* OrcRendererHandle;
typedef struct OrcAnalysisContext* OrcAnalysisHandle;

#ifdef __cplusplus
}
#endif

#endif // ORC_PUBLIC_TYPES_H
