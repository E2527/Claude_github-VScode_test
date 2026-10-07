// Background scenes for the top screen and the story transitions between them.
#pragma once

void scenes_init(void);
void scenes_update(float dt, float pulse, float level);   // once per frame
void scenes_draw(void);                                   // once per eye
void scenes_next(void);
void scenes_resize(void);
int scenes_index(void);
int scenes_count(void);
const char *scenes_name(void);
int scenes_busy(void);   // a transition is playing
