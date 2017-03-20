# Window-TinyLFU Cache
This is a barebones C++ header-only implementation of the state-of-the-art cache admission policy proposed in [this paper](https://arxiv.org/abs/1512.00727) with details borrowed from [Caffeine's](https://github.com/ben-manes/caffeine) own implementation.

### Note
My original use case for this cache was very specific, so some features are absent - most notably thread-safety and hash collision protection.
