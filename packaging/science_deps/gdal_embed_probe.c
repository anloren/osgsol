#if !defined(__has_embed)
#error "C23 #embed is unavailable"
#elif __has_embed(__FILE__) == 0
#error "C23 #embed cannot read the probe source"
#endif

static const unsigned char g_probe[] = {
#embed __FILE__
};

int main(void)
{
    return g_probe[0] == 0;
}
