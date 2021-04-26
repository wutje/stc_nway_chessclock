#ifndef BUTTONS_H
#define BUTTONS_H

enum ButtonEvent {
    EV_NONE,
    EV_S1_SHORT,
    EV_S1_LONG,
    EV_S2_SHORT,
    EV_S2_LONG,
    EV_S1S2_LONG,
    EV_S3_SHORT,
    EV_S3_LONG,
    EV_TIMEOUT,
};

extern enum ButtonEvent event;

void buttons_read(void);

#endif /* BUTTONS_H */
