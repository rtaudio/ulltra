int
hogcpu(void);

int
hogio();


int
hogvm(long long bytes, long long stride, long long hang, int keep);

int
hoghdd(long long bytes);


void stressStop();

void stressStartSetFlag();