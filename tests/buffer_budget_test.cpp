#include "signalscope/backend/iq/iq.hpp"
#include <iostream>
using namespace signalscope;
int main() {
    BoundedQueue<Bytes> queue(16, [](const Bytes &bytes) { return bytes.size(); });
    for (int i = 0; i < 8; ++i)
        if (!queue.try_push(Bytes(2, std::uint8_t(i))))
            return 1;
    if (queue.try_push(Bytes(1)) || queue.high_water_weight() != 16)
        return 2;
    Bytes bytes;
    for (int i = 0; i < 4; ++i)
        if (!queue.pop(bytes) || bytes.front() != i)
            return 3;
    if (!queue.try_push(Bytes(8, 9)) || queue.used_weight() != 16)
        return 4;
    queue.close();
    if (queue.try_push(Bytes(1)))
        return 5;
    for (int i = 4; i < 8; ++i)
        if (!queue.pop(bytes) || bytes.front() != i)
            return 6;
    if (!queue.pop(bytes) || bytes.size() != 8 || bytes.front() != 9 || queue.pop(bytes) || queue.used_weight() != 0)
        return 7;
    BoundedQueue<int> count(2);
    if (!count.try_push(1) || !count.try_push(2) || count.try_push(3))
        return 8;
    std::cout << "Payload budgets preserve short reads, FIFO, backpressure, draining and count-based callers.\n";
    return 0;
}
