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

package org.apache.ignite.internal;

import static java.lang.Math.max;
import static org.apache.ignite.internal.hlc.HybridTimestamp.LOGICAL_TIME_BITS_SIZE;
import static org.apache.ignite.internal.hlc.HybridTimestamp.hybridTimestamp;

import java.lang.invoke.MethodHandles;
import java.lang.invoke.VarHandle;
import java.util.function.LongSupplier;
import org.apache.ignite.internal.hlc.HybridClock;
import org.apache.ignite.internal.hlc.HybridTimestamp;
import org.apache.ignite.internal.tostring.S;

/**
 * Test hybrid clock with custom supplier of current time.
 */
public class TestHybridClock implements HybridClock {
    /** Supplier of current time in milliseconds. */
    private final LongSupplier currentTimeMillisSupplier;

    /** Latest time. */
    private volatile long latestTime;

    /**
     * Var handle for {@link #latestTime}.
     */
    private static final VarHandle LATEST_TIME;

    static {
        try {
            LATEST_TIME = MethodHandles.lookup().findVarHandle(TestHybridClock.class, "latestTime", long.class);
        } catch (NoSuchFieldException | IllegalAccessException e) {
            throw new ExceptionInInitializerError(e);
        }
    }

    public TestHybridClock(LongSupplier currentTimeMillisSupplier) {
        this.currentTimeMillisSupplier = currentTimeMillisSupplier;
        this.latestTime = currentTime();
    }

    private long currentTime() {
        return currentTimeMillisSupplier.getAsLong() << LOGICAL_TIME_BITS_SIZE;
    }

    @Override
    public long nowLong() {
        while (true) {
            long now = currentTime();

            // Read the latest time after accessing UTC time to reduce contention.
            long oldLatestTime = latestTime;

            long newLatestTime = max(oldLatestTime + 1, now);

            if (LATEST_TIME.compareAndSet(this, oldLatestTime, newLatestTime)) {
                return newLatestTime;
            }
        }
    }

    @Override
    public HybridTimestamp now() {
        return hybridTimestamp(nowLong());
    }

    /**
     * Creates a timestamp for a received event.
     *
     * @param requestTime Timestamp from request.
     * @return The hybrid timestamp.
     */
    @Override
    public HybridTimestamp update(HybridTimestamp requestTime) {
        while (true) {
            long now = currentTime();

            // Read the latest time after accessing UTC time to reduce contention.
            long oldLatestTime = this.latestTime;

            long newLatestTime = max(requestTime.longValue() + 1, max(now, oldLatestTime + 1));

            if (LATEST_TIME.compareAndSet(this, oldLatestTime, newLatestTime)) {
                return hybridTimestamp(newLatestTime);
            }
        }
    }

    /** {@inheritDoc} */
    @Override
    public String toString() {
        return S.toString(HybridClock.class, this);
    }
}
