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

package org.apache.ignite.internal.deployunit.metastore;

import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.function.Predicate;
import java.util.stream.Collectors;
import org.apache.ignite.internal.deployunit.UnitStatus;
import org.apache.ignite.internal.deployunit.UnitStatuses;
import org.apache.ignite.internal.deployunit.UnitStatuses.UnitStatusesBuilder;
import org.apache.ignite.internal.deployunit.metastore.key.UnitMetaSerializer;
import org.apache.ignite.internal.metastorage.Entry;
import org.apache.ignite.internal.util.subscription.AccumulateException;
import org.apache.ignite.internal.util.subscription.Accumulator;

/**
 * Units accumulator with filtering mechanism.
 */
public class UnitsAccumulator implements Accumulator<Entry, List<UnitStatuses>> {
    private final Map<String, UnitStatusesBuilder> map = new HashMap<>();

    private final Predicate<UnitStatus> filter;

    public UnitsAccumulator() {
        this(t -> true);
    }

    public UnitsAccumulator(Predicate<UnitStatus> filter) {
        this.filter = filter;
    }

    @Override
    public void accumulate(Entry item) {
        UnitStatus meta = UnitMetaSerializer.deserialize(item.value());
        if (filter.test(meta)) {
            map.computeIfAbsent(meta.id(), UnitStatuses::builder)
                    .append(meta.version(), meta.status()).build();
        }
    }

    @Override
    public List<UnitStatuses> get() throws AccumulateException {
        return map.values().stream().map(UnitStatusesBuilder::build).collect(Collectors.toList());
    }
}
