#ifndef LIBRARY_MANAGER_H
#define LIBRARY_MANAGER_H

#include "common.h"

Result library_init(void);
Result library_scan(const char *folder_path,
                    void (*progress_cb)(const char *title, const char *artist,
                                        float percent));
Result library_reset(void);
Result
library_get_tracks(void); /* Placeholder for getting tracks into player */

#endif /* LIBRARY_MANAGER_H */
