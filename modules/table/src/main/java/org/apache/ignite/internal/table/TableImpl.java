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

package org.apache.ignite.internal.table;

import static java.util.concurrent.CompletableFuture.allOf;

import java.util.ArrayList;
import java.util.Collection;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.UUID;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.ConcurrentHashMap;
import java.util.function.Function;
import java.util.function.Supplier;
import org.apache.ignite.internal.schema.BinaryRow;
import org.apache.ignite.internal.schema.BinaryRowEx;
import org.apache.ignite.internal.schema.BinaryTuple;
import org.apache.ignite.internal.schema.SchemaRegistry;
import org.apache.ignite.internal.schema.marshaller.MarshallerException;
import org.apache.ignite.internal.schema.marshaller.TupleMarshallerException;
import org.apache.ignite.internal.schema.marshaller.TupleMarshallerImpl;
import org.apache.ignite.internal.schema.marshaller.reflection.KvMarshallerImpl;
import org.apache.ignite.internal.schema.row.Row;
import org.apache.ignite.internal.storage.index.HashIndexDescriptor;
import org.apache.ignite.internal.storage.index.SortedIndexDescriptor;
import org.apache.ignite.internal.table.distributed.HashIndexLocker;
import org.apache.ignite.internal.table.distributed.IndexLocker;
import org.apache.ignite.internal.table.distributed.SortedIndexLocker;
import org.apache.ignite.internal.table.distributed.TableIndexStoragesSupplier;
import org.apache.ignite.internal.table.distributed.TableSchemaAwareIndexStorage;
import org.apache.ignite.internal.tx.LockManager;
import org.apache.ignite.lang.ErrorGroups;
import org.apache.ignite.lang.IgniteInternalException;
import org.apache.ignite.network.ClusterNode;
import org.apache.ignite.table.KeyValueView;
import org.apache.ignite.table.RecordView;
import org.apache.ignite.table.Table;
import org.apache.ignite.table.Tuple;
import org.apache.ignite.table.mapper.Mapper;
import org.jetbrains.annotations.TestOnly;

/**
 * Table view implementation for binary objects.
 */
public class TableImpl implements Table {
    /** Internal table. */
    private final InternalTable tbl;

    private final LockManager lockManager;

    /** Schema registry. Should be set either in constructor or via {@link #schemaView(SchemaRegistry)} before start of using the table. */
    private volatile SchemaRegistry schemaReg;

    private final CompletableFuture<UUID> pkId = new CompletableFuture<>();

    private final Map<UUID, CompletableFuture<?>> indexesToWait = new ConcurrentHashMap<>();

    private final Map<UUID, IndexStorageAdapterFactory> indexStorageAdapterFactories = new ConcurrentHashMap<>();
    private final Map<UUID, IndexLockerFactory> indexLockerFactories = new ConcurrentHashMap<>();

    /**
     * Constructor.
     *
     * @param tbl       The table.
     * @param lockManager Lock manager.
     */
    public TableImpl(InternalTable tbl, LockManager lockManager) {
        this.tbl = tbl;
        this.lockManager = lockManager;
    }

    /**
     * Constructor.
     *
     * @param tbl The table.
     * @param schemaReg Table schema registry.
     * @param lockManager Lock manager.
     */
    @TestOnly
    public TableImpl(InternalTable tbl, SchemaRegistry schemaReg, LockManager lockManager) {
        this.tbl = tbl;
        this.schemaReg = schemaReg;
        this.lockManager = lockManager;
    }

    /**
     * Gets a table id.
     *
     * @return Table id as UUID.
     */
    public UUID tableId() {
        return tbl.tableId();
    }

    /**
     * Provides current table with notion of a primary index.
     *
     * @param pkId An identifier of a primary index.
     */
    public void pkId(UUID pkId) {
        this.pkId.complete(Objects.requireNonNull(pkId, "pkId"));
    }

    /** Returns an identifier of a primary index. */
    public UUID pkId() {
        return pkId.join();
    }

    /** Returns an internal table instance this view represents. */
    public InternalTable internalTable() {
        return tbl;
    }

    @Override public String name() {
        return tbl.name();
    }

    /**
     * Gets a schema view for the table.
     *
     * @return Schema view.
     */
    public SchemaRegistry schemaView() {
        return schemaReg;
    }

    /**
     * Sets a schema view for the table.
     */
    public void schemaView(SchemaRegistry schemaReg) {
        assert this.schemaReg == null : "Schema registry is already set [tableName=" + name() + ']';

        Objects.requireNonNull(schemaReg, "Schema registry must not be null [tableName=" + name() + ']');

        this.schemaReg = schemaReg;
    }

    @Override
    public <R> RecordView<R> recordView(Mapper<R> recMapper) {
        return new RecordViewImpl<>(tbl, schemaReg, recMapper);
    }

    @Override
    public RecordView<Tuple> recordView() {
        return new RecordBinaryViewImpl(tbl, schemaReg);
    }

    @Override
    public <K, V> KeyValueView<K, V> keyValueView(Mapper<K> keyMapper, Mapper<V> valMapper) {
        return new KeyValueViewImpl<>(tbl, schemaReg, keyMapper, valMapper);
    }

    @Override
    public KeyValueView<Tuple, Tuple> keyValueView() {
        return new KeyValueBinaryViewImpl(tbl, schemaReg);
    }

    /**
     * Returns a partition for a key tuple.
     *
     * @param key The tuple.
     * @return The partition.
     */
    public int partition(Tuple key) {
        Objects.requireNonNull(key);

        try {
            final Row keyRow = new TupleMarshallerImpl(schemaReg).marshalKey(key);

            return tbl.partition(keyRow);
        } catch (TupleMarshallerException e) {
            throw new IgniteInternalException(e);
        }
    }

    /**
     * Returns a partition for a key.
     *
     * @param key The key.
     * @param keyMapper Key mapper
     * @return The partition.
     */
    public <K> int partition(K key, Mapper<K> keyMapper) {
        Objects.requireNonNull(key);
        Objects.requireNonNull(keyMapper);

        BinaryRowEx keyRow;
        var marshaller = new KvMarshallerImpl<>(schemaReg.schema(), keyMapper, keyMapper);
        try {
            keyRow = marshaller.marshal(key);
        } catch (MarshallerException e) {
            throw new IgniteInternalException("Cannot marshal key", e);
        }

        return tbl.partition(keyRow);
    }

    /**
     * Returns cluster node that is the leader of the corresponding partition group or throws an exception if
     * it cannot be found.
     *
     * @param partition partition number
     * @return leader node of the partition group corresponding to the partition
     */
    public ClusterNode leaderAssignment(int partition) {
        return tbl.leaderAssignment(partition);
    }

    /** Returns a supplier of index storage wrapper factories for given partition. */
    public TableIndexStoragesSupplier indexStorageAdapters(int partId) {
        return new TableIndexStoragesSupplier() {
            @Override
            public Map<UUID, TableSchemaAwareIndexStorage> get() {
                awaitIndexes();

                List<IndexStorageAdapterFactory> factories = new ArrayList<>(indexStorageAdapterFactories.values());

                Map<UUID, TableSchemaAwareIndexStorage> adapters = new HashMap<>();

                for (IndexStorageAdapterFactory factory : factories) {
                    TableSchemaAwareIndexStorage storage = factory.create(partId);
                    adapters.put(storage.id(), storage);
                }

                return adapters;
            }

            @Override
            public void addIndexToWaitIfAbsent(UUID indexId) {
                addIndexesToWait(List.of(indexId));
            }
        };
    }

    /** Returns a supplier of index locker factories for given partition. */
    public Supplier<Map<UUID, IndexLocker>> indexesLockers(int partId) {
        return () -> {
            awaitIndexes();

            List<IndexLockerFactory> factories = new ArrayList<>(indexLockerFactories.values());

            Map<UUID, IndexLocker> lockers = new HashMap<>(factories.size());

            for (IndexLockerFactory factory : factories) {
                IndexLocker locker = factory.create(partId);
                lockers.put(locker.id(), locker);
            }

            return lockers;
        };
    }

    /**
     * The future completes when the primary key index is ready to use.
     *
     * @return Future whcih complete when a primary index for the table is .
     */
    public CompletableFuture<Void> pkIndexesReadyFuture() {
        var fut = new CompletableFuture<Void>();

        pkId.whenComplete((uuid, throwable) -> fut.complete(null));

        return fut;
    }

    /**
     * Register the index with given id in a table.
     *
     * @param indexDescriptor Index descriptor.
     * @param unique A flag indicating whether the given index unique or not.
     * @param searchRowResolver Function which converts given table row to an index key.
     */
    public void registerHashIndex(
            HashIndexDescriptor indexDescriptor,
            boolean unique,
            Function<BinaryRow, BinaryTuple> searchRowResolver
    ) {
        UUID indexId = indexDescriptor.id();

        indexLockerFactories.put(
                indexId,
                partitionId -> new HashIndexLocker(
                        indexId,
                        unique,
                        lockManager,
                        searchRowResolver
                )
        );
        indexStorageAdapterFactories.put(
                indexId,
                partitionId -> new TableSchemaAwareIndexStorage(
                        indexId,
                        tbl.storage().getOrCreateHashIndex(partitionId, indexDescriptor),
                        searchRowResolver
                )
        );

        completeWaitIndex(indexId);
    }

    /**
     * Register the index with given id in a table.
     *
     * @param indexDescriptor Index descriptor.
     * @param searchRowResolver Function which converts given table row to an index key.
     */
    public void registerSortedIndex(
            SortedIndexDescriptor indexDescriptor,
            Function<BinaryRow, BinaryTuple> searchRowResolver
    ) {
        UUID indexId = indexDescriptor.id();

        indexLockerFactories.put(
                indexId,
                partitionId -> new SortedIndexLocker(
                        indexId,
                        partitionId,
                        lockManager,
                        tbl.storage().getOrCreateSortedIndex(partitionId, indexDescriptor),
                        searchRowResolver
                )
        );
        indexStorageAdapterFactories.put(
                indexId,
                partitionId -> new TableSchemaAwareIndexStorage(
                        indexId,
                        tbl.storage().getOrCreateSortedIndex(partitionId, indexDescriptor),
                        searchRowResolver
                )
        );

        completeWaitIndex(indexId);
    }

    /**
     * Unregister given index from table.
     *
     * @param indexId An index id to unregister.
     */
    public void unregisterIndex(UUID indexId) {
        indexLockerFactories.remove(indexId);
        indexStorageAdapterFactories.remove(indexId);

        completeWaitIndex(indexId);

        // TODO: IGNITE-19150 Also need to destroy the index storages
    }

    private void awaitIndexes() {
        List<CompletableFuture<?>> toWait = new ArrayList<>();

        toWait.add(pkId);

        toWait.addAll(indexesToWait.values());

        allOf(toWait.toArray(CompletableFuture[]::new)).join();
    }

    /**
     * Prepares this table for being closed.
     */
    public void beforeClose() {
        IgniteInternalException closeTableException = new IgniteInternalException(
                ErrorGroups.Table.TABLE_STOPPING_ERR,
                "Table is being stopped: tableId=" + tableId()
        );

        pkId.completeExceptionally(closeTableException);

        indexesToWait.values().forEach(future -> future.completeExceptionally(closeTableException));
    }

    @FunctionalInterface
    private interface IndexLockerFactory {
        /** Creates the index decorator for given partition. */
        IndexLocker create(int partitionId);
    }

    @FunctionalInterface
    private interface IndexStorageAdapterFactory {
        /** Creates the index decorator for given partition. */
        TableSchemaAwareIndexStorage create(int partitionId);
    }

    /**
     * Adds indexes to wait, if not already created, before inserting data into the table.
     *
     * @param indexIds Indexes Index IDs.
     */
    // TODO: IGNITE-19082 Needs to be redone/improved
    public void addIndexesToWait(Collection<UUID> indexIds) {
        for (UUID indexId : indexIds) {
            indexesToWait.compute(indexId, (indexId0, awaitIndexFuture) -> {
                if (awaitIndexFuture != null) {
                    return awaitIndexFuture;
                }

                if (indexStorageAdapterFactories.containsKey(indexId) && indexLockerFactories.containsKey(indexId)) {
                    // Index is already registered and created.
                    return null;
                }

                return new CompletableFuture<>();
            });
        }
    }

    private void completeWaitIndex(UUID indexId) {
        CompletableFuture<?> indexToWaitFuture = indexesToWait.remove(indexId);

        if (indexToWaitFuture != null) {
            indexToWaitFuture.complete(null);
        }
    }
}
