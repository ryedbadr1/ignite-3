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

#pragma once

namespace ignite {

/**
 * Simple abstraction for value, that have default value but can be set to a different value.
 *
 * @tparam T Type of the value.
 */
template<typename T>
class settable_value
{
public:
    /** Type of the value. */
    typedef T value_type;

    /**
     * Constructor.
     *
     * @param default_value Default value to return when is not set.
     */
    explicit settable_value(const value_type& default_value)
        : m_value(default_value) { }

    /**
     * Set non-default value.
     *
     * @param value Value.
     * @param dflt Set value as default or not.
     */
    void set_value(const value_type& value, bool dflt = false)
    {
        m_value = value;
        m_set = !dflt;
    }

    /**
     * Get value.
     *
     * @return Value or default value if not set.
     */
    const value_type& get_value() const
    {
        return m_value;
    }

    /**
     * Check whether value is set to non-default.
     */
    [[nodiscard]] bool is_set() const
    {
        return m_set;
    }

private:
    /** Current value. */
    value_type m_value{};

    /** Flag showing whether value was set to non-default value. */
    bool m_set{false};
};

} // namespace ignite
