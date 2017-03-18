
#ifndef AMAP_H
#define AMAP_H

//#include <cobc/device_driver/spacewire/spacewire_printer.h>
#include <cobc/device_driver/amap/amap.h>

void
printErrorCounter(const cobc::iff::Amap::ErrorCounter& errorCounter);

void
pingAmap(cobc::iff::Amap * amap);

#endif // AMAP_H
