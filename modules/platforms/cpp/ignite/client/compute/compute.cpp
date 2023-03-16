/*
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements. See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The ASF licenses this file to You under the Apache License, Version 2.0
 * (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "ignite/client/compute/compute.h"
#include "ignite/client/detail/argument_check_utils.h"
#include "ignite/client/detail/compute/compute_impl.h"

#include <random>

namespace ignite {

template<typename T>
typename T::value_type get_random_element(const T &cont) {
    // TODO: Move to utils
    static std::mutex randomMutex;
    static std::random_device rd;
    static std::mt19937 gen(rd());

    assert(!cont.empty());

    std::uniform_int_distribution<size_t> distrib(0, cont.size() - 1);

    std::lock_guard<std::mutex> lock(randomMutex);

    return cont[distrib(gen)];
}

void compute::execute_async(const std::vector<cluster_node>& nodes, std::string_view job_class_name,
    const std::vector<primitive>& args, ignite_callback<std::optional<primitive>> callback) {
    detail::arg_check::container_non_empty(nodes, "Nodes container");
    detail::arg_check::container_non_empty(job_class_name, "Job class name");

    m_impl->execute_on_one_node(get_random_element(nodes), job_class_name, args, std::move(callback));
}

void compute::broadcast_async(const std::set<cluster_node>& nodes, std::string_view job_class_name,
    const std::vector<primitive>& args,
    ignite_callback<std::map<cluster_node, ignite_result<std::optional<primitive>>>> callback) {
    typedef std::map<cluster_node, ignite_result<std::optional<primitive>>> result_type;

    detail::arg_check::container_non_empty(nodes, "Nodes set");
    detail::arg_check::container_non_empty(job_class_name, "Job class name");

    struct result_group {
        explicit result_group(std::int32_t cnt, ignite_callback<result_type> &&cb) : m_cnt(cnt), m_callback(cb) {}

        std::mutex m_mutex;
        result_type m_res_map;
        ignite_callback<result_type> m_callback;
        std::int32_t m_cnt{0};
    };

    auto shared_res = std::make_shared<result_group>(std::int32_t(nodes.size()), std::move(callback));

    for (const auto &node : nodes) {
        m_impl->execute_on_one_node(node, job_class_name, args, [node, shared_res](auto &&res) {
            auto &val = *shared_res;

            std::lock_guard<std::mutex> lock(val.m_mutex);
            val.m_res_map.emplace(node, res);
            --val.m_cnt;
            if (val.m_cnt == 0)
                val.m_callback(std::move(val.m_res_map));
        });
    }
}

void compute::execute_colocated_async(const std::string &table_name, const ignite_tuple &key,
    std::string_view job_class_name, const std::vector<primitive> &args,
    ignite_callback<std::optional<primitive>> callback) {
    detail::arg_check::container_non_empty(table_name, "Table name");
    detail::arg_check::container_non_empty(job_class_name, "Job class name");

    // TODO: Implement me.
}

} // namespace ignite