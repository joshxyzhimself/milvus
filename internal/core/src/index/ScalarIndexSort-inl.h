// Copyright (C) 2019-2020 Zilliz. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except in compliance
// with the License. You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied. See the License for the specific language governing permissions and limitations under the License

#include <algorithm>
#include <memory>
#include <utility>
#include <pb/schema.pb.h>
#include <vector>
#include <string>
#include "knowhere/common/Log.h"

namespace milvus::scalar {

template <typename T>
inline ScalarIndexSort<T>::ScalarIndexSort() : is_built_(false), data_() {
}

template <typename T>
inline ScalarIndexSort<T>::ScalarIndexSort(const size_t n, const T* values) : is_built_(false) {
    ScalarIndexSort<T>::Build(n, values);
}

template <typename T>
inline void
ScalarIndexSort<T>::Build(const DatasetPtr& dataset) {
    auto size = dataset->Get<int64_t>(knowhere::meta::ROWS);
    auto data = dataset->Get<const void*>(knowhere::meta::TENSOR);
    Build(size, reinterpret_cast<const T*>(data));
}

template <typename T>
inline void
ScalarIndexSort<T>::Build(const size_t n, const T* values) {
    data_.reserve(n);
    T* p = const_cast<T*>(values);
    for (size_t i = 0; i < n; ++i) {
        data_.emplace_back(IndexStructure(*p++, i));
    }
    build();
}

template <typename T>
inline void
ScalarIndexSort<T>::build() {
    if (is_built_)
        return;
    if (data_.size() == 0) {
        // todo: throw an exception
        throw std::invalid_argument("ScalarIndexSort cannot build null values!");
    }
    std::sort(data_.begin(), data_.end());
    is_built_ = true;
}

template <typename T>
inline BinarySet
ScalarIndexSort<T>::Serialize(const Config& config) {
    if (!is_built_) {
        build();
    }

    auto index_data_size = data_.size() * sizeof(IndexStructure<T>);
    std::shared_ptr<uint8_t[]> index_data(new uint8_t[index_data_size]);
    memcpy(index_data.get(), data_.data(), index_data_size);

    std::shared_ptr<uint8_t[]> index_length(new uint8_t[sizeof(size_t)]);
    auto index_size = data_.size();
    memcpy(index_length.get(), &index_size, sizeof(size_t));

    BinarySet res_set;
    res_set.Append("index_data", index_data, index_data_size);
    res_set.Append("index_length", index_length, sizeof(size_t));
    return res_set;
}

template <typename T>
inline void
ScalarIndexSort<T>::Load(const BinarySet& index_binary) {
    size_t index_size;
    auto index_length = index_binary.GetByName("index_length");
    memcpy(&index_size, index_length->data.get(), (size_t)index_length->size);

    auto index_data = index_binary.GetByName("index_data");
    data_.resize(index_size);
    memcpy(data_.data(), index_data->data.get(), (size_t)index_data->size);
    is_built_ = true;
}

template <typename T>
inline const TargetBitmapPtr
ScalarIndexSort<T>::In(const size_t n, const T* values) {
    if (!is_built_) {
        build();
    }
    TargetBitmapPtr bitset = std::make_unique<TargetBitmap>(data_.size());
    for (size_t i = 0; i < n; ++i) {
        auto lb = std::lower_bound(data_.begin(), data_.end(), IndexStructure<T>(*(values + i)));
        auto ub = std::upper_bound(data_.begin(), data_.end(), IndexStructure<T>(*(values + i)));
        for (; lb < ub; ++lb) {
            if (lb->a_ != *(values + i)) {
                std::cout << "error happens in ScalarIndexSort<T>::In, experted value is: " << *(values + i)
                          << ", but real value is: " << lb->a_;
            }
            bitset->set(lb->idx_);
        }
    }
    return bitset;
}

template <typename T>
inline const TargetBitmapPtr
ScalarIndexSort<T>::NotIn(const size_t n, const T* values) {
    if (!is_built_) {
        build();
    }
    TargetBitmapPtr bitset = std::make_unique<TargetBitmap>(data_.size());
    bitset->set();
    for (size_t i = 0; i < n; ++i) {
        auto lb = std::lower_bound(data_.begin(), data_.end(), IndexStructure<T>(*(values + i)));
        auto ub = std::upper_bound(data_.begin(), data_.end(), IndexStructure<T>(*(values + i)));
        for (; lb < ub; ++lb) {
            if (lb->a_ != *(values + i)) {
                std::cout << "error happens in ScalarIndexSort<T>::NotIn, experted value is: " << *(values + i)
                          << ", but real value is: " << lb->a_;
            }
            bitset->reset(lb->idx_);
        }
    }
    return bitset;
}

template <typename T>
inline const TargetBitmapPtr
ScalarIndexSort<T>::Range(const T value, const OperatorType op) {
    if (!is_built_) {
        build();
    }
    TargetBitmapPtr bitset = std::make_unique<TargetBitmap>(data_.size());
    auto lb = data_.begin();
    auto ub = data_.end();
    switch (op) {
        case OperatorType::LT:
            ub = std::lower_bound(data_.begin(), data_.end(), IndexStructure<T>(value));
            break;
        case OperatorType::LE:
            ub = std::upper_bound(data_.begin(), data_.end(), IndexStructure<T>(value));
            break;
        case OperatorType::GT:
            lb = std::upper_bound(data_.begin(), data_.end(), IndexStructure<T>(value));
            break;
        case OperatorType::GE:
            lb = std::lower_bound(data_.begin(), data_.end(), IndexStructure<T>(value));
            break;
        default:
            throw std::invalid_argument(std::string("Invalid OperatorType: ") + std::to_string((int)op) + "!");
    }
    for (; lb < ub; ++lb) {
        bitset->set(lb->idx_);
    }
    return bitset;
}

template <typename T>
inline const TargetBitmapPtr
ScalarIndexSort<T>::Range(T lower_bound_value, bool lb_inclusive, T upper_bound_value, bool ub_inclusive) {
    if (!is_built_) {
        build();
    }
    TargetBitmapPtr bitset = std::make_unique<TargetBitmap>(data_.size());
    if (lower_bound_value > upper_bound_value) {
        std::swap(lower_bound_value, upper_bound_value);
        std::swap(lb_inclusive, ub_inclusive);
    }
    auto lb = data_.begin();
    auto ub = data_.end();
    if (lb_inclusive) {
        lb = std::lower_bound(data_.begin(), data_.end(), IndexStructure<T>(lower_bound_value));
    } else {
        lb = std::upper_bound(data_.begin(), data_.end(), IndexStructure<T>(lower_bound_value));
    }
    if (ub_inclusive) {
        ub = std::upper_bound(data_.begin(), data_.end(), IndexStructure<T>(upper_bound_value));
    } else {
        ub = std::lower_bound(data_.begin(), data_.end(), IndexStructure<T>(upper_bound_value));
    }
    for (; lb < ub; ++lb) {
        bitset->set(lb->idx_);
    }
    return bitset;
}

template <>
inline void
ScalarIndexSort<std::string>::Build(const milvus::scalar::DatasetPtr& dataset) {
    auto size = dataset->Get<int64_t>(knowhere::meta::ROWS);
    auto data = dataset->Get<const void*>(knowhere::meta::TENSOR);
    proto::schema::StringArray arr;
    arr.ParseFromArray(data, size);
    // TODO: optimize here. avoid memory copy.
    std::vector<std::string> vecs{arr.data().begin(), arr.data().end()};
    Build(arr.data().size(), vecs.data());
}

template <>
inline BinarySet
ScalarIndexSort<std::string>::Serialize(const Config& config) {
    BinarySet res_set;
    auto data = this->GetData();
    for (const auto& record : data) {
        auto idx = record.idx_;
        auto str = record.a_;
        std::shared_ptr<uint8_t[]> content(new uint8_t[str.length()]);
        memcpy(content.get(), str.c_str(), str.length());
        res_set.Append(std::to_string(idx), content, str.length());
    }
    return res_set;
}

template <>
inline void
ScalarIndexSort<std::string>::Load(const BinarySet& index_binary) {
    std::vector<std::string> vecs;

    for (const auto& [k, v] : index_binary.binary_map_) {
        std::string str(reinterpret_cast<const char*>(v->data.get()), v->size);
        vecs.emplace_back(str);
    }

    Build(vecs.size(), vecs.data());
}

}  // namespace milvus::scalar
