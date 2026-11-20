#ifndef DOIPLITE_EXPORT_H
#define DOIPLITE_EXPORT_H

#ifdef __cplusplus
#define DOIPLITE_EXTERN_C_BEGIN extern "C" {
#define DOIPLITE_EXTERN_C_END   }
#else
#define DOIPLITE_EXTERN_C_BEGIN
#define DOIPLITE_EXTERN_C_END
#endif

#define DOIPLITE_API

#endif
