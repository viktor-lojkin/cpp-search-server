#pragma once

#include <iostream>
#include <vector>


template <typename Iterator>
class IteratorRange {
public:
    IteratorRange(Iterator begin, Iterator end) :
        begin_(begin), end_(end) {}
    
    Iterator begin() const {
        return begin_;
    }

    Iterator end() const {
        return end_;
    }

    int size() const {
        return distance(begin_, end_);
    }

private:
    Iterator begin_;
    Iterator end_;
};


template <typename Iterator>
class Paginator {
public:

    Paginator(Iterator begin, Iterator end, size_t page_size) {
        auto buffer = begin;
        advance(buffer, page_size);
        while (buffer < end) {
            pages_.push_back({ begin, buffer });
            begin = buffer;
            advance(buffer, page_size);
        }
        pages_.push_back({ begin, end });
    }

    auto begin() const {
        return pages_.begin();
    }

    auto end() const {
        return pages_.end();
    }

    int size() const {
        return distance(pages_.begin(), pages_.end());
    }

private:
    std::vector<IteratorRange<Iterator>> pages_;
};


template <typename Container>
auto Paginate(const Container& c, size_t page_size) {
    return Paginator(begin(c), end(c), page_size);
}


template <typename Iterator>
std::ostream& operator<<(std::ostream& out, IteratorRange<Iterator> page) {
    for (auto it = page.begin(); it < page.end(); ++it) {
        out << *it;
    }
    return out;
}