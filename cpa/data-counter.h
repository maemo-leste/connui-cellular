#ifndef DATACOUNTER_H
#define DATACOUNTER_H

void cellular_data_counter_destroy();
void cellular_data_counter_save();
void cellular_data_counter_reset();
GtkDialog *cellular_data_counter_show(GtkWindow *parent, gboolean home_counter);

#endif // DATACOUNTER_H
