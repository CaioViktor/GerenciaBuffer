/*-------------------------------------------------------------------------
 *
 * freelist.c
 *	  routines for managing the buffer pool's replacement strategy.
 *
 *
 * Portions Copyright (c) 1996-2016, PostgreSQL Global Development Group
 * Portions Copyright (c) 1994, Regents of the University of California
 *
 *
 * IDENTIFICATION
 *	  src/backend/storage/buffer/freelist.c
 *
 *-------------------------------------------------------------------------
 */
#include "postgres.h"

#include "port/atomics.h"
#include "storage/buf_internals.h"
#include "storage/bufmgr.h"
#include "storage/proc.h"

#define INT_ACCESS_ONCE(var)	((int)(*((volatile int *)&(var))))




/*
 * The shared freelist control information.
 */
typedef struct
{
	/* Spinlock: protects the values below */
	slock_t		buffer_strategy_lock;

	//Código Original - INÍCIO

	/*
	 * Clock sweep hand: index of next buffer to consider grabbing. Note that
	 * this isn't a concrete buffer - we only ever increase the value. So, to
	 * get an actual buffer, it needs to be used modulo NBuffers.
	 */
	// pg_atomic_uint32 nextVictimBuffer;

	// int			firstFreeBuffer;/*	 Head of list of unused buffers */
	// int			lastFreeBuffer; /* Tail of list of unused buffers */

	/*
	 * NOTE: lastFreeBuffer is undefined when firstFreeBuffer is -1 (that is,
	 * when the list is empty)
	 */

	/*
	 * Statistics.  These counters should be wide enough that they can't
	 * overflow during a single bgwriter cycle.
	 */
	uint32		completePasses; /* Complete cycles of the clock sweep */
	pg_atomic_uint32 numBufferAllocs;	/* Buffers allocated since last reset */

	//Código Original - FIM
	
	//Meu Código - INÍCIO

	BufferPool *pool;

	//Meu Código - FIM
	/*
	 * Bgworker process to be notified upon activity or -1 if none. See
	 * StrategyNotifyBgWriter.
	 */
	int			bgwprocno;
} BufferStrategyControl;

/* Pointers to shared state */
static BufferStrategyControl *StrategyControl = NULL;

/*
 * Private (non-shared) state for managing a ring of shared buffers to re-use.
 * This is currently the only kind of BufferAccessStrategy object, but someday
 * we might have more kinds.
 */
typedef struct BufferAccessStrategyData
{
	/* Overall strategy type */
	BufferAccessStrategyType btype;
	/* Number of elements in buffers[] array */
	int			ring_size;

	/*
	 * Index of the "current" slot in the ring, ie, the one most recently
	 * returned by GetBufferFromRing.
	 */
	int			current;

	/*
	 * True if the buffer just returned by StrategyGetBuffer had been in the
	 * ring already.
	 */
	bool		current_was_in_ring;

	/*
	 * Array of buffer numbers.  InvalidBuffer (that is, zero) indicates we
	 * have not yet selected a buffer for this ring slot.  For allocation
	 * simplicity this is palloc'd together with the fixed fields of the
	 * struct.
	 */
	Buffer		buffers[FLEXIBLE_ARRAY_MEMBER];
}	BufferAccessStrategyData;


/* Prototypes for internal functions */
static BufferDesc *GetBufferFromRing(BufferAccessStrategy strategy,
				  uint32 *buf_state);
static void AddBufferToRing(BufferAccessStrategy strategy,
				BufferDesc *buf);


//Código original - INÍCIO

/*
 * ClockSweepTick - Helper routine for StrategyGetBuffer()
 *
 * Move the clock hand one buffer ahead of its current position and return the
 * id of the buffer now under the hand.
 */
// static inline uint32
// ClockSweepTick(void)
// {
// 	uint32		victim;

// 	/*
// 	 * Atomically move hand ahead one buffer - if there's several processes
// 	 * doing this, this can lead to buffers being returned slightly out of
// 	 * apparent order.
// 	 */
// 	victim =
// 		pg_atomic_fetch_add_u32(&StrategyControl->nextVictimBuffer, 1);

// 	if (victim >= NBuffers)
// 	{
// 		uint32		originalVictim = victim;

// 		/* always wrap what we look up in BufferDescriptors */
// 		victim = victim % NBuffers;

		
// 		 /* If we're the one that just caused a wraparound, force
// 		 * completePasses to be incremented while holding the spinlock. We
// 		 * need the spinlock so StrategySyncStart() can return a consistent
// 		 * value consisting of nextVictimBuffer and completePasses.
// 		 */
// 		if (victim == 0)
// 		{
// 			uint32		expected;
// 			uint32		wrapped;
// 			bool		success = false;

// 			expected = originalVictim + 1;

// 			while (!success)
// 			{
// 				/*
// 				 * Acquire the spinlock while increasing completePasses. That
// 				 * allows other readers to read nextVictimBuffer and
// 				 * completePasses in a consistent manner which is required for
// 				 * StrategySyncStart().  In theory delaying the increment
// 				 * could lead to an overflow of nextVictimBuffers, but that's
// 				 * highly unlikely and wouldn't be particularly harmful.
// 				 */
// 				SpinLockAcquire(&StrategyControl->buffer_strategy_lock);

// 				wrapped = expected % NBuffers;

// 				success = pg_atomic_compare_exchange_u32(&StrategyControl->nextVictimBuffer,
// 														 &expected, wrapped);
// 				if (success)
// 					StrategyControl->completePasses++;
// 				SpinLockRelease(&StrategyControl->buffer_strategy_lock);
// 			}
// 		}
// 	}
// 	return victim;
// }



//Código original - FIM



/*
 * StrategyGetBuffer
 *
 *	Called by the bufmgr to get the next candidate buffer to use in
 *	BufferAlloc(). The only hard requirement BufferAlloc() has is that
 *	the selected buffer must not currently be pinned by anyone.
 *
 *	strategy is a BufferAccessStrategy object, or NULL for default strategy.
 *
 *	To ensure that no one else can pin the buffer before we do, we must
 *	return the buffer with the buffer header spinlock still held.
 */
BufferDesc *
StrategyGetBuffer(BufferAccessStrategy strategy, uint32 *buf_state,BlockNumber pageId)
{
	BufferDesc *buf;
	BufferN *buffer;
	int			bgwprocno;
	int			trycounter;
	uint32		local_buf_state;	/* to avoid repeated (de-)referencing */
	int estavaFantasma;
	int vezesLoop;

	/*
	 * If given a strategy object, see whether it can select a buffer. We
	 * assume strategy objects don't need buffer_strategy_lock.
	 */
	if (strategy != NULL)
	{
		buf = GetBufferFromRing(strategy, buf_state);
		if (buf != NULL)
			return buf;
	}

	/*
	 * If asked, we need to waken the bgwriter. Since we don't want to rely on
	 * a spinlock for this we force a read from shared memory once, and then
	 * set the latch based on that value. We need to go through that length
	 * because otherwise bgprocno might be reset while/after we check because
	 * the compiler might just reread from memory.
	 *
	 * This can possibly set the latch of the wrong process if the bgwriter
	 * dies in the wrong moment. But since PGPROC->procLatch is never
	 * deallocated the worst consequence of that is that we set the latch of
	 * some arbitrary process.
	 */
	bgwprocno = INT_ACCESS_ONCE(StrategyControl->bgwprocno);
	if (bgwprocno != -1)
	{
		/* reset bgwprocno first, before setting the latch */
		StrategyControl->bgwprocno = -1;

		/*
		 * Not acquiring ProcArrayLock here which is slightly icky. It's
		 * actually fine because procLatch isn't ever freed, so we just can
		 * potentially set the wrong process' (or no process') latch.
		 */
		SetLatch(&ProcGlobal->allProcs[bgwprocno].procLatch);
	}

	/*
	 * We count buffer allocation requests so that the bgwriter can estimate
	 * the rate of buffer consumption.  Note that buffers recycled by a
	 * strategy object are intentionally not counted here.
	 */
	pg_atomic_fetch_add_u32(&StrategyControl->numBufferAllocs, 1);

	/*
	 * First check, without acquiring the lock, whether there's buffers in the
	 * freelist. Since we otherwise don't require the spinlock in every
	 * StrategyGetBuffer() invocation, it'd be sad to acquire it here -
	 * uselessly in most cases. That obviously leaves a race where a buffer is
	 * put on the freelist but we don't see the store yet - but that's pretty
	 * harmless, it'll just get used during the next buffer acquisition.
	 *
	 * If there's buffers on the freelist, acquire the spinlock to pop one
	 * buffer of the freelist. Then check whether that buffer is usable and
	 * repeat if not.
	 *
	 * Note that the freeNext fields are considered to be protected by the
	 * buffer_strategy_lock not the individual buffer spinlocks, so it's OK to
	 * manipulate them without holding the spinlock.
	 */

	//Código original - INÍCIO


	// if (StrategyControl->firstFreeBuffer >= 0)
	// {
	// 	while (true)
	// 	{
	// 		/* Acquire the spinlock to remove element from the freelist */
	// 		SpinLockAcquire(&StrategyControl->buffer_strategy_lock);

	// 		if (StrategyControl->firstFreeBuffer < 0)
	// 		{
	// 			SpinLockRelease(&StrategyControl->buffer_strategy_lock);
	// 			break;
	// 		}

	// 		buf = GetBufferDescriptor(StrategyControl->firstFreeBuffer);
	// 		Assert(buf->freeNext != FREENEXT_NOT_IN_LIST);

	// 		/* Unconditionally remove buffer from freelist */
	// 		StrategyControl->firstFreeBuffer = buf->freeNext;
	// 		buf->freeNext = FREENEXT_NOT_IN_LIST;

	// 		/*
	// 		 * Release the lock so someone else can access the freelist while
	// 		 * we check out this buffer.
	// 		 */
	// 		SpinLockRelease(&StrategyControl->buffer_strategy_lock);

	// 		/*
	// 		 * If the buffer is pinned or has a nonzero usage_count, we cannot
	// 		 * use it; discard it and retry.  (This can only happen if VACUUM
	// 		 * put a valid buffer in the freelist and then someone else used
	// 		 * it before we got to it.  It's probably impossible altogether as
	// 		 * of 8.3, but we'd better check anyway.)
	// 		 */
	// 		local_buf_state = LockBufHdr(buf);
	// 		if (BUF_STATE_GET_REFCOUNT(local_buf_state) == 0
	// 			&& BUF_STATE_GET_USAGECOUNT(local_buf_state) == 0)
	// 		{
	// 			if (strategy != NULL)
	// 				AddBufferToRing(strategy, buf);
	// 			*buf_state = local_buf_state;
	// 			// return buf;
	// 		}
	// 		UnlockBufHdr(buf, local_buf_state);

	// 	}
	// }





	// /* Nothing on the freelist, so run the "clock sweep" algorithm */
	// trycounter = NBuffers;
	// for (;;)
	// {
	// 	buf = GetBufferDescriptor(ClockSweepTick());

	// 	/*
	// 	 * If the buffer is pinned or has a nonzero usage_count, we cannot use
	// 	 * it; decrement the usage_count (unless pinned) and keep scanning.
	// 	 */
	// 	local_buf_state = LockBufHdr(buf);

	// 	if (BUF_STATE_GET_REFCOUNT(local_buf_state) == 0)
	// 		{
	// 		if (BUF_STATE_GET_USAGECOUNT(local_buf_state) != 0)
	// 		{
	// 			local_buf_state -= BUF_USAGECOUNT_ONE;

	// 			trycounter = NBuffers;
	// 		}
	// 		else
	// 		{
	// 			/* Found a usable buffer */
	// 			if (strategy != NULL)
	// 				AddBufferToRing(strategy, buf);
	// 			*buf_state = local_buf_state;
	// 			return buf;
	// 		}
	// 	}
	// 	else if (--trycounter == 0)
	// 	{
	// 		/*
	// 		 * We've scanned all the buffers without making any state changes,
	// 		 * so all the buffers are pinned (or were when we looked at them).
	// 		 * We could hope that someone will free one eventually, but it's
	// 		 * probably better to fail than to risk getting stuck in an
	// 		 * infinite loop.
	// 		 */
	// 		UnlockBufHdr(buf, local_buf_state);
	// 		elog(ERROR, "no unpinned buffers available");
	// 	}
	// 	UnlockBufHdr(buf, local_buf_state);
	// }


	//Código original - FIM


	//Meu Código - INÍCIO
	SpinLockAcquire(&StrategyControl->buffer_strategy_lock);
	vezesLoop = StrategyControl->pool->areaLeitura->tamanhoMaximo;
	trycounter = vezesLoop;
	SpinLockRelease(&StrategyControl->buffer_strategy_lock);
	while(trycounter > 0){
		SpinLockAcquire(&StrategyControl->buffer_strategy_lock);
		//printf("vai atualizarConselheiros\n");
		estavaFantasma = atualizarConselheiros(StrategyControl->pool,pageId);
		//printf("foi atualizarConselheiros\n");
		if(estavaFantasma != 0 ){//estava em lista fantasma
			//printf("vai inserir leitura 1\n");
			buffer = realInserir(StrategyControl->pool->areaLeitura,1,pageId);
			//printf("inseriu leitura 1\n");
		}
		else{
			// //printf("leitura!!!!\n");
			//printf("vai inserir leitura 0\n");
			buffer = realInserir(StrategyControl->pool->areaLeitura,0,pageId);
			//printf("inseriu leitura 0\n");	
		}

		if(buffer == NULL){
			// printf("erro ao conseguir buffer livre para %d\n",pageId);
			SpinLockRelease(&StrategyControl->buffer_strategy_lock);
			elog(ERROR, "erro ao conseguir buffer livre para");
		}

		buf = GetBufferDescriptor(buffer->idBuffer);
		Assert(buf->freeNext != FREENEXT_NOT_IN_LIST);
		buf->freeNext = FREENEXT_NOT_IN_LIST;
		

		SpinLockRelease(&StrategyControl->buffer_strategy_lock);

		local_buf_state = LockBufHdr(buf);
		//printf("vai vem state\n");
		if (BUF_STATE_GET_REFCOUNT(local_buf_state) == 0
			&& BUF_STATE_GET_USAGECOUNT(local_buf_state) == 0
			)
		{
			// printf("vai retornar buffer\n");
			if (strategy != NULL)
				AddBufferToRing(strategy, buf);
			buffer->usos =	0;
			*buf_state = local_buf_state;
			return buf;
		}else{
			// printf("buffer nao aceito\n");
			if (BUF_STATE_GET_REFCOUNT(local_buf_state) == 0)
				{
				if (BUF_STATE_GET_USAGECOUNT(local_buf_state) != 0)
				{
					// elog(NOTICE, "Entrou");
					local_buf_state -= BUF_USAGECOUNT_ONE;
					UnlockBufHdr(buf, local_buf_state);
					SpinLockAcquire(&StrategyControl->buffer_strategy_lock);
					// printf("vai colocar buffer devolta\n");
					if(estavaFantasma != 0 ){//estava em lista fantasma
						buffer = realRemovePorIdPagina(StrategyControl->pool->areaLeitura,1,pageId);
					}
					else{
						buffer = realRemovePorIdPagina(StrategyControl->pool->areaLeitura,0,pageId);
					}
					if(buffer == NULL){
						SpinLockRelease(&StrategyControl->buffer_strategy_lock);
						// printf("deu erro ao remover buffer inserido para volta aos livres\n");
						elog(ERROR, "deu erro ao remover buffer inserido para volta aos livres");
					}
					realInserirBuffer(StrategyControl->pool->areaLeitura,2,buffer);
					trycounter = vezesLoop;
					// printf("colocou buffer devolta\n");
					SpinLockRelease(&StrategyControl->buffer_strategy_lock);
				}
				else
				{
					/* Found a usable buffer */
					// printf("buffer deu certo no fim\n");
					if (strategy != NULL)
						AddBufferToRing(strategy, buf);
					*buf_state = local_buf_state;
					return buf;
				}
			}
		}
		trycounter--;
	}
	

	// printf("erro buffer\n");
	// UnlockBufHdr(buf, local_buf_state);
	elog(ERROR, "no unpinned buffers available");
		
	//Meu Código - FIM

}

/*
 * StrategyFreeBuffer: put a buffer on the freelist
 */
void
StrategyFreeBuffer(BufferDesc *buf)
{
	//printf("Pondo buffer livre\n");
	BufferN *block = NULL;
	SpinLockAcquire(&StrategyControl->buffer_strategy_lock);



	//Meu Código - INÍCIO

	block = realRemovePorIdBuffer(StrategyControl->pool->areaLeitura,0,buf->buf_id);
	if(block != NULL){
		block->usos = 0;
		realInserirBuffer(StrategyControl->pool->areaLeitura,2,block);
		// //printf("frequencia leitura\n");
	}else{
		block = realRemovePorIdBuffer(StrategyControl->pool->areaLeitura,1,buf->buf_id);
		if(block != NULL){
			block->usos = 0;
			realInserirBuffer(StrategyControl->pool->areaLeitura,2,block);
			// //printf("frequencia escrita\n");		
		}else{
			block = realRemovePorIdBuffer(StrategyControl->pool->areaEscrita,0,buf->buf_id);
			if(block != NULL){
				block->usos = 0;
				realInserirBuffer(StrategyControl->pool->areaEscrita,2,block);
				// //printf("frequencia escrita\n");		
			}else{
				block = realRemovePorIdBuffer(StrategyControl->pool->areaEscrita,1,buf->buf_id);
				if(block != NULL){
					block->usos = 0;
					realInserirBuffer(StrategyControl->pool->areaEscrita,2,block);
					// //printf("frequencia escrita\n");		
				}else{
					// printf("Não consegui acrescentar buffer livre %d",buf->buf_id);
					SpinLockRelease(&StrategyControl->buffer_strategy_lock);
					elog(ERROR, "Não consegui acrescentar buffer livre");
				}
			}
		}
	}

	//Meu Código - FIM

	//Código Original - INÍCIO
	/*
	 * It is possible that we are told to put something in the freelist that
	 * is already in it; don't screw up the list if so.
	 */
	// if (buf->freeNext == FREENEXT_NOT_IN_LIST)
	// {
	// 	buf->freeNext = StrategyControl->firstFreeBuffer;
	// 	if (buf->freeNext < 0)
	// 		StrategyControl->lastFreeBuffer = buf->buf_id;
	// 	StrategyControl->firstFreeBuffer = buf->buf_id;
	// }
	//Código Original - FIM

	SpinLockRelease(&StrategyControl->buffer_strategy_lock);
}

/*
 * StrategySyncStart -- tell BufferSync where to start syncing
 *
 * The result is the buffer index of the best buffer to sync first.
 * BufferSync() will proceed circularly around the buffer array from there.
 *
 * In addition, we return the completed-pass count (which is effectively
 * the higher-order bits of nextVictimBuffer) and the count of recent buffer
 * allocs if non-NULL pointers are passed.  The alloc count is reset after
 * being read.
 */
int
StrategySyncStart(uint32 *complete_passes, uint32 *num_buf_alloc)
{
	uint32		nextVictimBuffer;
	int			result;

	SpinLockAcquire(&StrategyControl->buffer_strategy_lock);

	//Meu Código - INÍCIO
	// printf("Sync");
	result = StrategyControl->pool->areaLeitura->livres->inicio->bufferCabeca->idBuffer;
	nextVictimBuffer = StrategyControl->pool->areaLeitura->livres->tamanhoMaximo - StrategyControl->pool->areaLeitura->livres->tamanhoAtual;//Não sei qual deveria ser esse valor(pode vir a dar um erro bem cabulozo, mas deixa para depois)
	//Meu Código - FIM

	//Código originial - INÍCIO
	// nextVictimBuffer = pg_atomic_read_u32(&StrategyControl->nextVictimBuffer);
	// result = nextVictimBuffer % NBuffers;
	//Código originial - FIM

	if (complete_passes)
	{
		*complete_passes = StrategyControl->completePasses;

		/*
		 * Additionally add the number of wraparounds that happened before
		 * completePasses could be incremented. C.f. ClockSweepTick().
		 */
		*complete_passes += nextVictimBuffer / NBuffers;
	}

	if (num_buf_alloc)
	{
		*num_buf_alloc = pg_atomic_exchange_u32(&StrategyControl->numBufferAllocs, 0);
	}
	SpinLockRelease(&StrategyControl->buffer_strategy_lock);
	return result;
}

/*
 * StrategyNotifyBgWriter -- set or clear allocation notification latch
 *
 * If bgwriterLatch isn't NULL, the next invocation of StrategyGetBuffer will
 * set that latch.  Pass NULL to clear the pending notification before it
 * happens.  This feature is used by the bgwriter process to wake itself up
 * from hibernation, and is not meant for anybody else to use.
 */
void
StrategyNotifyBgWriter(int bgwprocno)
{
	/*
	 * We acquire buffer_strategy_lock just to ensure that the store appears
	 * atomic to StrategyGetBuffer.  The bgwriter should call this rather
	 * infrequently, so there's no performance penalty from being safe.
	 */
	SpinLockAcquire(&StrategyControl->buffer_strategy_lock);
	StrategyControl->bgwprocno = bgwprocno;
	SpinLockRelease(&StrategyControl->buffer_strategy_lock);
}


/*
 * StrategyShmemSize
 *
 * estimate the size of shared memory used by the freelist-related structures.
 *
 * Note: for somewhat historical reasons, the buffer lookup hashtable size
 * is also determined here.
 */
Size
StrategyShmemSize(void)
{
	Size		size = 0;

	/* size of lookup hash table ... see comment in StrategyInitialize */
	size = add_size(size, BufTableShmemSize(NBuffers + NUM_BUFFER_PARTITIONS));

	/* size of the shared replacement strategy control block */
	size = add_size(size, MAXALIGN(sizeof(BufferStrategyControl)));

	return size;
}

/*
 * StrategyInitialize -- initialize the buffer cache replacement
 *		strategy.
 *
 * Assumes: All of the buffers are already built into a linked list.
 *		Only called by postmaster and only during initialization.
 */
void
StrategyInitialize(bool init)
{
	bool		found;

	/*
	 * Initialize the shared buffer lookup hashtable.
	 *
	 * Since we can't tolerate running out of lookup table entries, we must be
	 * sure to specify an adequate table size here.  The maximum steady-state
	 * usage is of course NBuffers entries, but BufferAlloc() tries to insert
	 * a new entry before deleting the old.  In principle this could be
	 * happening in each partition concurrently, so we could need as many as
	 * NBuffers + NUM_BUFFER_PARTITIONS entries.
	 */
	InitBufTable(NBuffers + NUM_BUFFER_PARTITIONS);

	/*
	 * Get or create the shared strategy control block
	 */
	StrategyControl = (BufferStrategyControl *)
		ShmemInitStruct("Buffer Strategy Status",
						sizeof(BufferStrategyControl),
						&found);

	if (!found)
	{
		/*
		 * Only done once, usually in postmaster
		 */
		Assert(init);

		SpinLockInit(&StrategyControl->buffer_strategy_lock);
		
		//Meu código - INÍCIO
		StrategyControl->pool = (BufferPool*) malloc(sizeof(BufferPool));
		initBufferPoolN(StrategyControl->pool,NBuffers);

		//Meu código - FIM


		//Código original - INÍCIO



		/*
		 * Grab the whole linked list of free buffers for our strategy. We
		 * assume it was previously set up by InitBufferPool().
		 */
		// StrategyControl->firstFreeBuffer = 0;
		// StrategyControl->lastFreeBuffer = NBuffers - 1;

		// /* Initialize the clock sweep pointer */
		// pg_atomic_init_u32(&StrategyControl->nextVictimBuffer, 0);

		// /* Clear statistics */
		// StrategyControl->completePasses = 0;
		// pg_atomic_init_u32(&StrategyControl->numBufferAllocs, 0);



		//Código original - FIM

		/* No pending notification */
		StrategyControl->bgwprocno = -1;
	}
	else
		Assert(!init);
}


/* ----------------------------------------------------------------
 *				Backend-private buffer ring management
 * ----------------------------------------------------------------
 */


/*
 * GetAccessStrategy -- create a BufferAccessStrategy object
 *
 * The object is allocated in the current memory context.
 */
BufferAccessStrategy
GetAccessStrategy(BufferAccessStrategyType btype)
{
	BufferAccessStrategy strategy;
	int			ring_size;

	/*
	 * Select ring size to use.  See buffer/README for rationales.
	 *
	 * Note: if you change the ring size for BAS_BULKREAD, see also
	 * SYNC_SCAN_REPORT_INTERVAL in access/heap/syncscan.c.
	 */
	switch (btype)
	{
		case BAS_NORMAL:
			/* if someone asks for NORMAL, just give 'em a "default" object */
			return NULL;

		case BAS_BULKREAD:
			ring_size = 256 * 1024 / BLCKSZ;
			break;
		case BAS_BULKWRITE:
			ring_size = 16 * 1024 * 1024 / BLCKSZ;
			break;
		case BAS_VACUUM:
			ring_size = 256 * 1024 / BLCKSZ;
			break;

		default:
			elog(ERROR, "unrecognized buffer access strategy: %d",
				 (int) btype);
			return NULL;		/* keep compiler quiet */
	}

	/* Make sure ring isn't an undue fraction of shared buffers */
	ring_size = Min(NBuffers / 8, ring_size);

	/* Allocate the object and initialize all elements to zeroes */
	strategy = (BufferAccessStrategy)
		palloc0(offsetof(BufferAccessStrategyData, buffers) +
				ring_size * sizeof(Buffer));

	/* Set fields that don't start out zero */
	strategy->btype = btype;
	strategy->ring_size = ring_size;

	return strategy;
}

/*
 * FreeAccessStrategy -- release a BufferAccessStrategy object
 *
 * A simple pfree would do at the moment, but we would prefer that callers
 * don't assume that much about the representation of BufferAccessStrategy.
 */
void
FreeAccessStrategy(BufferAccessStrategy strategy)
{
	/* don't crash if called on a "default" strategy */
	if (strategy != NULL)
		pfree(strategy);
}

/*
 * GetBufferFromRing -- returns a buffer from the ring, or NULL if the
 *		ring is empty.
 *
 * The bufhdr spin lock is held on the returned buffer.
 */
static BufferDesc *
GetBufferFromRing(BufferAccessStrategy strategy, uint32 *buf_state)
{
	BufferDesc *buf;
	Buffer		bufnum;
	uint32		local_buf_state;	/* to avoid repeated (de-)referencing */


	/* Advance to next ring slot */
	if (++strategy->current >= strategy->ring_size)
		strategy->current = 0;

	/*
	 * If the slot hasn't been filled yet, tell the caller to allocate a new
	 * buffer with the normal allocation strategy.  He will then fill this
	 * slot by calling AddBufferToRing with the new buffer.
	 */
	bufnum = strategy->buffers[strategy->current];
	if (bufnum == InvalidBuffer)
	{
		strategy->current_was_in_ring = false;
		return NULL;
	}

	/*
	 * If the buffer is pinned we cannot use it under any circumstances.
	 *
	 * If usage_count is 0 or 1 then the buffer is fair game (we expect 1,
	 * since our own previous usage of the ring element would have left it
	 * there, but it might've been decremented by clock sweep since then). A
	 * higher usage_count indicates someone else has touched the buffer, so we
	 * shouldn't re-use it.
	 */
	buf = GetBufferDescriptor(bufnum - 1);
	local_buf_state = LockBufHdr(buf);
	if (BUF_STATE_GET_REFCOUNT(local_buf_state) == 0
		&& BUF_STATE_GET_USAGECOUNT(local_buf_state) <= 1)
	{
		strategy->current_was_in_ring = true;
		*buf_state = local_buf_state;
		return buf;
	}
	UnlockBufHdr(buf, local_buf_state);

	/*
	 * Tell caller to allocate a new buffer with the normal allocation
	 * strategy.  He'll then replace this ring element via AddBufferToRing.
	 */
	strategy->current_was_in_ring = false;
	return NULL;
}

/*
 * AddBufferToRing -- add a buffer to the buffer ring
 *
 * Caller must hold the buffer header spinlock on the buffer.  Since this
 * is called with the spinlock held, it had better be quite cheap.
 */
static void
AddBufferToRing(BufferAccessStrategy strategy, BufferDesc *buf)
{
	strategy->buffers[strategy->current] = BufferDescriptorGetBuffer(buf);
}

/*
 * StrategyRejectBuffer -- consider rejecting a dirty buffer
 *
 * When a nondefault strategy is used, the buffer manager calls this function
 * when it turns out that the buffer selected by StrategyGetBuffer needs to
 * be written out and doing so would require flushing WAL too.  This gives us
 * a chance to choose a different victim.
 *
 * Returns true if buffer manager should ask for a new victim, and false
 * if this buffer should be written and re-used.
 */
bool
StrategyRejectBuffer(BufferAccessStrategy strategy, BufferDesc *buf)
{
	/* We only do this in bulkread mode */
	if (strategy->btype != BAS_BULKREAD)
		return false;

	/* Don't muck with behavior of normal buffer-replacement strategy */
	if (!strategy->current_was_in_ring ||
	  strategy->buffers[strategy->current] != BufferDescriptorGetBuffer(buf))
		return false;

	/*
	 * Remove the dirty buffer from the ring; necessary to prevent infinite
	 * loop if all ring members are dirty.
	 */
	strategy->buffers[strategy->current] = InvalidBuffer;

	return true;
}




//Meu Código - subrotinas de acesso externo
void hitBuffer(int idBuffer,BlockNumber idBlock){
	BufferN* block;
	block = NULL;
	int estavaFantasma = atualizarConselheiros(StrategyControl->pool,idBlock);
	// elog(NOTICE,"Entrou hitbuffer");
	//printf("entrou bufferhit %d\n",bufferId);
	SpinLockAcquire(&StrategyControl->buffer_strategy_lock);
	//printf("conseguiu lock\n");
	if(!(StrategyControl->pool->areaLeitura->tamanhoAtual + StrategyControl->pool->areaLeitura->tamanhoMaximoFantasma >= NBuffers)){//Basta olhar apenas na de leitura, pois a página só é escrita posteriormente
		//printf("vai seguirConselhos\n");
		seguirConselhos(StrategyControl->pool);
		//printf("foi seguirConselhos\n");
	}
	// elog(NOTICE,"segui conselhos");
	//Listas de frequência
	//printf("vai recuperar leitura 1\n");
	block = realRemovePorIdBuffer(StrategyControl->pool->areaLeitura,1,idBuffer);
	if(block != NULL){
		// elog(NOTICE,"hit leitura 1");
		block->usos++;
		realInserirBuffer(StrategyControl->pool->areaLeitura,1,block);
		//printf("frequencia leitura\n");
	}
	else{
		// elog(NOTICE,"vai recuperar es1");
		//printf("vai recuperar escrita 1\n");
		block = realRemovePorIdBuffer(StrategyControl->pool->areaLeitura,0,idBuffer);
		if(block != NULL){
			// realInserirBuffer(StrategyControl->pool->areaLeitura,2,block);
			// Pagina *pg = block->pagina;
			// block->pagina = NULL;
			// elog(NOTICE,"hit leitura 0");
			block->usos++;
			block = realInserirBufferExistente(StrategyControl->pool->areaLeitura,1,block);
			if(block!=NULL)
				realInserirBuffer(StrategyControl->pool->areaLeitura,2,block);
			//printf("resencia leitura\n");
		}
		
	}
	//Listas de frequência

	//Listas de resência
	if(block == NULL){
		block = realRemovePorIdBuffer(StrategyControl->pool->areaEscrita,1,idBuffer);
		if(block != NULL){
			// elog(NOTICE,"hit escrita 1");
			block->usos++;
			realInserirBuffer(StrategyControl->pool->areaEscrita,1,block);
			//printf("frequencia escrita\n");		
		}else{
			// elog(NOTICE,"vai recuperar es0");
			//printf("vai recuperar escrita 0\n");
			block = realRemovePorIdBuffer(StrategyControl->pool->areaEscrita,0,idBuffer);
			if(block != NULL){
				block->usos++;
				// elog(NOTICE,"hit escrita 0");
				// realInserirBuffer(StrategyControl->pool->areaEscrita,2,block);
				// Pagina *pg = block->pagina;
				// block->pagina = NULL;
				// elog(NOTICE,"vai recuperar es1");
				block = realInserirBufferExistente(StrategyControl->pool->areaEscrita,1,block);
				// elog(NOTICE,"terminou recuperar es1");
				if(block!=NULL)
					realInserirBuffer(StrategyControl->pool->areaEscrita,2,block);
				//printf("resencia escrita buffer \n");
				// elog(NOTICE,"saiu es1");
			}else{
				// elog(NOTICE,"Não conseguiu o hit");
				block = realRemovePorIdBuffer(StrategyControl->pool->areaLeitura,2,idBuffer);
				if(block == NULL){
					// elog(NOTICE,"vou buscar em escrita livre");
					block = realRemovePorIdBuffer(StrategyControl->pool->areaEscrita,2,idBuffer);
					if(block == NULL){
						SpinLockRelease(&StrategyControl->buffer_strategy_lock);
						elog(ERROR,"o bixo sumiu");
					}
					else{
						// elog(NOTICE,"estava escrita livre");
						block->idPagina = idBlock;
						if(!estavaFantasma)
							block = realInserirBufferExistente(StrategyControl->pool->areaEscrita,0,block);
						else
							block = realInserirBufferExistente(StrategyControl->pool->areaEscrita,1,block);
						// elog(NOTICE,"inseriu na lista");
						if(block!=NULL)
							realInserirBuffer(StrategyControl->pool->areaEscrita,2,block);
						// elog(NOTICE,"passou da livre");
					}
				}else{
					// elog(NOTICE,"estava leitura livre");
					block->idPagina = idBlock;
					if(!estavaFantasma)
						block = realInserirBufferExistente(StrategyControl->pool->areaLeitura,0,block);
					else
						block = realInserirBufferExistente(StrategyControl->pool->areaLeitura,1,block);
					// elog(NOTICE,"inseriu na lista");
					if(block!=NULL)
						realInserirBuffer(StrategyControl->pool->areaLeitura,2,block);
					// elog(NOTICE,"passou da livre");
				}
			}
		}	
	}
	// elog(NOTICE,"Vai sair de hitBuffer");
	SpinLockRelease(&StrategyControl->buffer_strategy_lock);
}


void marcarBufferEscrita(int idBuffer){
	BufferN *buffer;
	SpinLockAcquire(&StrategyControl->buffer_strategy_lock);



	buffer = realRemovePorIdBuffer(StrategyControl->pool->areaLeitura,1,idBuffer);
	if(buffer != NULL){
		buffer->usos++;
		// elog(NOTICE,"escrita leitura 1");
		buffer = realInserirBufferExistente(StrategyControl->pool->areaEscrita,1,buffer);
		realInserirBuffer(StrategyControl->pool->areaLeitura,2,buffer);
	}else{
		buffer = realRemovePorIdBuffer(StrategyControl->pool->areaLeitura,0,idBuffer);
		if(buffer != NULL){
			buffer->usos++;
			// elog(NOTICE,"escrita leitura 0");
			if(buffer->usos > 1)
				buffer = realInserirBufferExistente(StrategyControl->pool->areaEscrita,1,buffer);
			else
				buffer = realInserirBufferExistente(StrategyControl->pool->areaEscrita,0,buffer);
			if(buffer == NULL)
				buffer = realRemoveLRU(StrategyControl->pool->areaEscrita,2);
			if(buffer != NULL)
				realInserirBuffer(StrategyControl->pool->areaLeitura,2,buffer);
		}else{
			buffer = realRemovePorIdBuffer(StrategyControl->pool->areaEscrita,1,idBuffer);
			if(buffer != NULL){//Verifuca se buffer já havia sido adicionado em escrita frequente
				buffer->usos++;
				buffer = realInserirBufferExistente(StrategyControl->pool->areaEscrita,1,buffer);
				if(buffer != NULL)
					realInserirBuffer(StrategyControl->pool->areaEscrita,2,buffer);
				// elog(NOTICE,"escrita escrita 1");
			}else{
				buffer = realRemovePorIdBuffer(StrategyControl->pool->areaEscrita,0,idBuffer);
				if(buffer != NULL){//Verifuca se buffer já havia sido adicionado em escrita recente
					buffer->usos++;
					buffer = realInserirBufferExistente(StrategyControl->pool->areaEscrita,1,buffer);
					if(buffer != NULL)
						realInserirBuffer(StrategyControl->pool->areaEscrita,2,buffer);
					// elog(NOTICE,"escrita escrita 0");
				}
				else{
					printf("não consegui marcarBufferEscrita\n");
					SpinLockRelease(&StrategyControl->buffer_strategy_lock);
					elog(ERROR, "Não consegui marcarBufferEscrita");
				}	
			}
		}
	}

	// //printf("escrevendo\n");
	
	SpinLockRelease(&StrategyControl->buffer_strategy_lock);
}
//Meu Código - subrotinas de acesso externo

//Meu Código - INÍCIO

void initListaReal(ListaReal *lista, int tamanhoMaximo){
	
	lista->inicio = NULL;
	lista->fim = NULL;
	lista->tamanhoAtual = 0;
	lista->tamanhoMaximo = tamanhoMaximo;
}

void initListaFantasma(ListaFantasma *lista, int tamanhoMaximo){
	lista->inicio = NULL;
	lista->fim = NULL;
	lista->tamanhoAtual = 0;
	lista->tamanhoMaximo = tamanhoMaximo;
}

void initArea(AreaBuffer *area, int tamanhoMaximo){
	area->RA = malloc(sizeof(ListaReal));//0
	area->FA = malloc(sizeof(ListaReal));//1
	area->livres = malloc(sizeof(ListaReal));

	area->GRA = malloc(sizeof(ListaFantasma));//0
	area->GFA = malloc(sizeof(ListaFantasma));//1

	initListaReal(area->RA , ((int)tamanhoMaximo/2));
	initListaReal(area->FA , ((int)tamanhoMaximo/2));
	initListaReal(area->livres , tamanhoMaximo);

	initListaFantasma(area->GRA , ((int)tamanhoMaximo/2));
	initListaFantasma(area->GFA , ((int)tamanhoMaximo/2));

	area->tamanhoMaximo = tamanhoMaximo;
	area->tamanhoAtual = 0;
}

void initBufferPoolN(BufferPool* bufferPool,int buffer_size){
	int i = 1;

	//printf("Buffer size %d\n",buffer_size );
	bufferPool->areaEscrita = malloc(sizeof(AreaBuffer));//Talvez seja necessário usar palloc0
	bufferPool->areaLeitura = malloc(sizeof(AreaBuffer));

	initArea(bufferPool->areaEscrita,(int)buffer_size/2);
	initArea(bufferPool->areaLeitura,(int)buffer_size/2);

	bufferPool->conselheiroFrequenciaLeitura = 0;
	bufferPool->conselheiroFrequenciaEscrita = 0;
	bufferPool->conselheiroAreaEscrita = 0;
	
	for(i = 1 ; i <= (int)buffer_size/2 ; i++){
		BufferN *buffers1 = malloc(sizeof(BufferN));
		BufferN *buffers2 = malloc(sizeof(BufferN));
		
		buffers1->idBuffer = i;
		buffers1->idPagina = InvalidBlockNumber;
		buffers1->usos = 0;

		buffers2->idBuffer = i + (int)buffer_size/2;
		buffers2->idPagina = InvalidBlockNumber;
		buffers2->usos = 0;
		
		realInserirBuffer(bufferPool->areaEscrita,2,buffers1 );
		realInserirBuffer(bufferPool->areaLeitura,2,buffers2 );
	}

}

//Manipulação Lista Fantasma
ListaFantasma* getListFantasma(AreaBuffer *area,int list){
	if(list == 0)//GRA
		return area->GRA;
	else if(list == 1)//GFA
		return area->GFA;
	else 
		return NULL;
}


int fantasmaContem(AreaBuffer *area, int list, BlockNumber idPagina){
	ListaFantasma *lista;
	ElementoFantasma *elemento;
	lista = getListFantasma(area,list);
	elemento = lista->inicio;
	while(elemento != NULL){
		if(elemento->idPaginaFantasma== idPagina)
			return 1;
		elemento = elemento->cauda;
	}
	return 0;
}

int fantasmaRemoveLRU(AreaBuffer *area, int list){
	ListaFantasma *lista;
	ElementoFantasma *ret;
	int id;
	lista = getListFantasma(area,list);
	ret = NULL;
	if(lista->tamanhoAtual > 0){
		ret = lista->inicio;
		if(lista->tamanhoAtual == 1){
			lista->inicio = NULL;
			lista->fim = NULL;
		}else{
			lista->inicio = ret->cauda;
		}
		lista->tamanhoAtual--;
		area->tamanhoAtualFantasma--;
		id = ret->idPaginaFantasma;
		free(ret);
		// //printf("Removido do fantasma %d\n",id);
		return id;
	}
	return InvalidBlockNumber;
}

int fantasmaRemovePorIdPagina(AreaBuffer *area, int list, BlockNumber idPagina){
	ListaFantasma *lista;
	int id;
	ElementoFantasma *elementoAnterior;
	ElementoFantasma *elementoAtual;


	lista = getListFantasma(area,list);
	
	if(lista->tamanhoAtual == 0)
		return InvalidBlockNumber;
	
	if(lista->tamanhoAtual == 1 && lista->inicio->idPaginaFantasma==idPagina)
		return fantasmaRemoveLRU(area,list);

	if(lista->inicio != NULL && lista->inicio->idPaginaFantasma ==idPagina){
		ElementoFantasma *ret;
		ret = lista->inicio;
		lista->inicio = ret->cauda;
		id = ret->idPaginaFantasma;
		free(ret);
		lista->tamanhoAtual--;
		area->tamanhoAtualFantasma--;
		return id;
	}
	elementoAnterior = lista->inicio;
	elementoAtual = lista->inicio->cauda;
	while(elementoAtual != NULL){
		if(elementoAtual->idPaginaFantasma ==idPagina){
			elementoAnterior->cauda = elementoAtual->cauda;
			if(elementoAtual->cauda == NULL)
				lista->fim = elementoAnterior;
			lista->tamanhoAtual--;
			area->tamanhoAtualFantasma--;
			id = elementoAtual->idPaginaFantasma;
			free(elementoAtual);
			return id;
		}
		elementoAnterior = elementoAtual;
		elementoAtual = elementoAtual->cauda;
	}
	return InvalidBlockNumber;
}




int fantasmaInserir(AreaBuffer *area, int list, BlockNumber pageId){
	// if(area->tamanhoAtual >= area->tamanhoMaximo || list > 1)
	// 	return 0;

	ListaFantasma *lista;
	ElementoFantasma *elemento;
	lista = getListFantasma(area,list);
	

	if(lista->tamanhoAtual >= lista->tamanhoMaximo){
		fantasmaRemoveLRU(area,list);
		// //printf("Inserido na fatasma: %d\tRemovido Fatasma: %d\n",pageId,fantasmaRemoveLRU(area,list));
	}

	elemento = malloc(sizeof(ElementoFantasma));
	elemento->idPaginaFantasma = pageId;
	elemento->cauda = NULL;

	if(lista->tamanhoAtual == 0){
		lista->inicio = elemento;
		lista->fim = elemento;
	}else{
		lista->fim->cauda = elemento;
		lista->fim = elemento;
		// elemento->cauda = lista->inicio;
		// lista->inicio = elemento;
	}


	lista->tamanhoAtual++;
	area->tamanhoAtualFantasma++;
	return 1;
	
}
//Manipulação Lista Fantasma

//Manipulação Lista real
ListaReal* getList(AreaBuffer *area,int list){
	if(list == 0)//RA
		return area->RA;
	else if(list == 1)//FA
		return area->FA;
	else if(list == 2)
		return area->livres;
	else
		return NULL;

}ListaReal* getListContraria(AreaBuffer *area,int list){
	if(list == 1)//RA
		return area->RA;
	else if(list == 0)//FA
		return area->FA;
	else
		return NULL;
}

BufferN* realRemovePorIdBuffer(AreaBuffer *area, int list,int idBuffer){
	ListaReal *lista;
	ElementoReal *ret;
	ElementoReal *elementoAnterior;
	ElementoReal *elementoAtual;
	BufferN *bufret;
	lista = getList(area,list);
	// elog(NOTICE,"realRemovePorIdBuffer");
	if(lista->tamanhoAtual <= 0)
		return NULL;

	if(lista->tamanhoAtual == 1 && lista->inicio->bufferCabeca->idBuffer == idBuffer){
		// elog(NOTICE,"vai retornar lru");
		return realRemoveLRU(area,list);
	}

	if(lista->inicio != NULL && lista->inicio->bufferCabeca->idBuffer == idBuffer){
		// elog(NOTICE,"vai retorna inicio");
		ret = lista->inicio;
		lista->inicio = ret->cauda;
		lista->tamanhoAtual--;
		area->tamanhoAtual--;
		bufret = ret->bufferCabeca;
		free(ret);
		return bufret;
	}
	elementoAnterior = lista->inicio;
	elementoAtual = lista->inicio->cauda;
	while(elementoAtual != NULL){
		// elog(NOTICE,"Loop do realRemovePorIdBuffer");
		if(elementoAtual->bufferCabeca->idBuffer == idBuffer){
			elementoAnterior->cauda = elementoAtual->cauda;
			if(elementoAtual->cauda == NULL)
				lista->fim = elementoAnterior;
			lista->tamanhoAtual--;
			area->tamanhoAtual--;
			bufret = elementoAtual->bufferCabeca;
			free(elementoAtual);
			return bufret;
		}
		elementoAnterior = elementoAtual;
		elementoAtual = elementoAtual->cauda;
	}
	return NULL;
}

int realContem(AreaBuffer *area, int list, BlockNumber idPagina){
	ListaReal *lista;
	ElementoReal *elemento;
	lista = getList(area,list);
	elemento = lista->inicio;
	while(elemento != NULL){
		if((elemento->bufferCabeca != NULL) && (elemento->bufferCabeca->idPagina == idPagina))
			return 1;
		elemento = elemento->cauda;
	}
	return 0;
}

BufferN* realRemoveLRU(AreaBuffer *area, int list){
	// //printf("%d Removendo LRU\n",list);
	ListaReal *lista;
	ElementoReal *ret;
	BufferN *bufret;
	lista = getList(area,list);
	ret = NULL;
	if(lista->tamanhoAtual > 0){
		ret = lista->inicio;
		if(lista->tamanhoAtual == 1){
			lista->inicio = NULL;
			lista->fim = NULL;
			// //printf("Acertou ponteiros\n");
		}else{
			lista->inicio = ret->cauda;
		}

		lista->tamanhoAtual--;
		// //printf("Diminui contador\n");
		if(list == 0 || list == 1)
			area->tamanhoAtual--;
		bufret = ret->bufferCabeca;
		// //printf("BufferID removendo: %d\ttamanho: %d\tLista:%d\n",bufret->idBuffer,lista->tamanhoAtual,list);
		free(ret);
		// //printf("Deu free\n");
		return bufret;
	}
	// //printf("%d Não fez nada\n",list);
	return NULL;
}

BufferN* realRemovePorIdPagina(AreaBuffer *area, int list, BlockNumber idPagina){
	ListaReal *lista;
	ElementoReal *ret;
	ElementoReal *elementoAnterior;
	ElementoReal *elementoAtual;
	BufferN *bufret;
	lista = getList(area,list);
	if(lista->tamanhoAtual <= 0)
		return NULL;

	if(lista->tamanhoAtual == 1 && lista->inicio->bufferCabeca->idPagina == idPagina)
		return realRemoveLRU(area,list);

	if(lista->inicio != NULL && lista->inicio->bufferCabeca->idPagina == idPagina){
		ret = lista->inicio;
		lista->inicio = ret->cauda;
		lista->tamanhoAtual--;
		area->tamanhoAtual--;
		bufret = ret->bufferCabeca;
		free(ret);
		return bufret;
	}
	elementoAnterior = lista->inicio;
	elementoAtual = lista->inicio->cauda;
	while(elementoAtual != NULL){
		if(elementoAtual->bufferCabeca->idPagina == idPagina){
			elementoAnterior->cauda = elementoAtual->cauda;
			if(elementoAtual->cauda == NULL)
				lista->fim = elementoAnterior;
			lista->tamanhoAtual--;
			area->tamanhoAtual--;
			bufret = elementoAtual->bufferCabeca;
			free(elementoAtual);
			return bufret;
		}
		elementoAnterior = elementoAtual;
		elementoAtual = elementoAtual->cauda;
	}
	return NULL;
}


// void showArea(AreaBuffer *area){
// 	ListaReal *lista = NULL;
// 	ElementoReal *elemento;
// 	for(int i = 0 ; i < 3 ;i++){
// 		lista = getList(area,i);
// 		//printf("lista %d tamanhoAtual %d: ",i,lista->tamanhoAtual);
// 		elemento = lista->inicio;
// 		while(elemento != NULL){
// 			//printf(";buffer %d \t",elemento->bufferCabeca->idBuffer);
// 			if(BlockNumberIsValid(elemento->bufferCabeca->idPagina))
// 				//printf("pagina %d \t",elemento->bufferCabeca->idPagina);
			
// 			elemento = elemento->cauda;
// 		}
// 		//printf("\n\n");
// 	}
// }

int realInserirBuffer(AreaBuffer *area,int list ,BufferN *buffer){
	ListaReal *lista;
	ElementoReal *elemento;
	lista = getList(area,list);
	if(area->tamanhoAtual >= area->tamanhoMaximo || list > 2 || lista->tamanhoAtual >= lista->tamanhoMaximo)
		return 0;
	
	elemento = malloc(sizeof(ElementoReal));
	elemento->bufferCabeca = buffer;
	elemento->cauda = NULL;

	if(lista->tamanhoAtual == 0){
		lista->inicio = elemento;
		lista->fim = elemento;
	}else{
		lista->fim->cauda = elemento;
		lista->fim = elemento;
		// elemento->cauda = lista->inicio;
		// lista->inicio = elemento;
	}

	lista->tamanhoAtual++;
	if(list < 2)
		area->tamanhoAtual++;
	return 1;
	
}



BufferN* realInserir(AreaBuffer *area, int list, BlockNumber idPagina){
	// if(area->tamanhoAtual >= area->tamanhoMaximo || list > 1)
	// 	return 0;
	ElementoReal *elemento;
	BufferN *buffer;
	BlockNumber idPaginaAux;

	ListaReal *lista = getList(area,list);
	
	if(area->tamanhoAtual < area->tamanhoMaximo && lista->tamanhoAtual < lista->tamanhoMaximo){//Existe buffer livre na área
		buffer = realRemoveLRU(area,2);
		buffer->idPagina = idPagina;
		realInserirBuffer(area,list,buffer);
		return buffer;
	}else if(list >= 2)
		return NULL;

	//Passa LRU para fantasma
	// //printf("vai remover LRU para mandar para zona fantasma %d\n",lista->tamanhoMaximo);
	// showArea(area);
	buffer = realRemoveLRU(area,list);
	// //printf("BufferID: %d\n",buffer->idBuffer);
	idPaginaAux = buffer->idPagina;
	fantasmaInserir(area,list,idPaginaAux);
	// //printf("Está cheio \n",);
	// free(buffer->pagina);
	buffer->idPagina = idPagina;

	//Insere pagina na lista
	elemento = malloc(sizeof(ElementoReal));
	elemento->bufferCabeca = buffer;
	elemento->cauda = NULL;

	if(lista->tamanhoAtual == 0){
		lista->inicio = elemento;
		lista->fim = elemento;
	}else{
		lista->fim->cauda = elemento;
		lista->fim = elemento;
		// elemento->cauda = lista->inicio;
		// lista->inicio = elemento;
	}


	lista->tamanhoAtual++;
	area->tamanhoAtual++;
	return buffer;
	
}

BufferN* realInserirBufferExistente(AreaBuffer *area, int list,BufferN *buffer){
	// if(area->tamanhoAtual >= area->tamanhoMaximo || list > 1)
	// 	return 0;
	ElementoReal *elemento;
	BufferN *bufferret;
	ListaReal *lista = getList(area,list);
	bufferret = NULL;

	if(area->tamanhoAtual < area->tamanhoMaximo && lista->tamanhoAtual < lista->tamanhoMaximo){//Existe buffer livre na área
		bufferret = realRemoveLRU(area,2);
		realInserirBuffer(area,list,buffer);
		return bufferret;
	}else if(list >= 2)
		return NULL;

	//Passa LRU para fantasma
	// //printf("vai remover LRU para mandar para zona fantasma %d\n",lista->tamanhoMaximo);
	// showArea(area);
	bufferret = realRemoveLRU(area,list);
	// //printf("BufferID: %d\n",buffer->idBuffer);
	fantasmaInserir(area,list,bufferret->idPagina);
	// //printf("Está cheio \n",);
	// free(buffer->pagina);

	//Insere pagina na lista
	elemento = malloc(sizeof(ElementoReal));
	elemento->bufferCabeca = buffer;
	elemento->cauda = NULL;

	if(lista->tamanhoAtual == 0){
		lista->inicio = elemento;
		lista->fim = elemento;
	}else{
		lista->fim->cauda = elemento;
		lista->fim = elemento;
		// elemento->cauda = lista->inicio;
		// lista->inicio = elemento;
	}


	lista->tamanhoAtual++;
	area->tamanhoAtual++;
	return bufferret;
	
}

void normalizarLista(AreaBuffer *area,int list){
	ListaReal *lista;
	ListaReal *oposta;
	lista = getList(area,list);
	oposta = getList(area,!list);
	
	// ListaFantasma *fantasma = getListFantasma(area,list);
	// ListaFantasma *fantasmaOposta = getListFantasma(area,!list);

	while(lista->tamanhoMaximo <= 0){
		lista->tamanhoMaximo++;
		oposta->tamanhoMaximo--;

		// fantasma->tamanhoMaximo++;
		// fantasmaOposta->tamanhoMaximo--;
	}

	while(oposta->tamanhoMaximo <= 0){
		lista->tamanhoMaximo--;
		oposta->tamanhoMaximo++;

		// fantasma->tamanhoMaximo--;
		// fantasmaOposta->tamanhoMaximo++;
	}

	while(lista->tamanhoAtual > lista->tamanhoMaximo){
		BufferN *buffer;
		buffer = realRemoveLRU(area,list);
		buffer->idPagina = InvalidBlockNumber;
		realInserirBuffer(area,2,buffer);
	}
	// while(fantasma->tamanhoAtual > fantasma->tamanhoMaximo){
	// 	fantasmaRemoveLRU(area,list);
	// }
}

void conselhoListas(BufferPool *pool,int tipo){//tipo = 0 leitura, 1 escrita	
	
	if(tipo == 0){
		if(pool->conselheiroFrequenciaLeitura > 0){//Aumenta área de frequencia
			// //printf("aumenta frequencia leitura\n");

			pool->conselheiroFrequenciaLeitura--;

			pool->areaLeitura->FA->tamanhoMaximo++;
			// pool->areaLeitura->GFA->tamanhoMaximo++;
			
			while(pool->areaLeitura->FA->tamanhoMaximo + pool->areaLeitura->RA->tamanhoMaximo > pool->areaLeitura->tamanhoMaximo){
				pool->areaLeitura->RA->tamanhoMaximo--;
				// pool->areaLeitura->GRA->tamanhoMaximo--;
			}
		}else{//Aumenta área de recentes
			// //printf("aumenta recencia leitura\n");

			pool->conselheiroFrequenciaLeitura++;

			pool->areaLeitura->RA->tamanhoMaximo++;
			// pool->areaLeitura->GRA->tamanhoMaximo++;

			while(pool->areaLeitura->FA->tamanhoMaximo + pool->areaLeitura->RA->tamanhoMaximo > pool->areaLeitura->tamanhoMaximo){
				pool->areaLeitura->FA->tamanhoMaximo--;
				// pool->areaLeitura->GFA->tamanhoMaximo--;
			}
		}
	}else if(tipo == 1){

		if(pool->conselheiroFrequenciaEscrita > 0){//Aumenta área de frequencia
			// //printf("aumenta frequencia escrita\n");
			
			pool->conselheiroFrequenciaEscrita--;

			pool->areaEscrita->FA->tamanhoMaximo++;
			// pool->areaEscrita->GFA->tamanhoMaximo++;

			while(pool->areaEscrita->FA->tamanhoMaximo + pool->areaEscrita->RA->tamanhoMaximo > pool->areaEscrita->tamanhoMaximo){
				pool->areaEscrita->RA->tamanhoMaximo--;
				// pool->areaEscrita->GRA->tamanhoMaximo--;
			}
		}else{//Aumenta área de recentes
			// //printf("aumenta recencia escrita\n");

			pool->conselheiroFrequenciaEscrita++;

			pool->areaEscrita->RA->tamanhoMaximo++;
			// pool->areaEscrita->GRA->tamanhoMaximo++;

			while(pool->areaEscrita->FA->tamanhoMaximo + pool->areaEscrita->RA->tamanhoMaximo > pool->areaEscrita->tamanhoMaximo){
				pool->areaEscrita->FA->tamanhoMaximo--;
				// pool->areaEscrita->GFA->tamanhoMaximo--;
			}
		}
	}
}


void seguirConselhos(BufferPool *pool){
	
	int tamanhoAteriorLeitura = pool->areaLeitura->tamanhoMaximo;
	int tamanhoAteriorEscrita = pool->areaEscrita->tamanhoMaximo;
	BufferN *buffer;
	int deltaLeitura;
	int deltaEscrita;
	int deltaLeituraRA;
	int deltaLeituraFA;
	int deltaMaior;
	int deltaMenor;
	int deltaEscritaRA;
	int deltaEscritaFA;

	while(pool->conselheiroAreaEscrita != 0){
		if(pool->conselheiroAreaEscrita > 0){//Área de escrita deve aumentar
			// //printf("Aumentando área escrita\n");
			pool->conselheiroAreaEscrita--;

			pool->areaEscrita->tamanhoMaximo++;
			pool->areaEscrita->livres->tamanhoMaximo++;

			pool->areaLeitura->livres->tamanhoMaximo--;
			pool->areaLeitura->tamanhoMaximo--;

			buffer = realRemoveLRU(pool->areaLeitura,2);
			if(buffer == NULL){//remove um buffer livre da leitura
				//não há buffer de leitura livre
				if(pool->conselheiroFrequenciaLeitura < 0){
					// //printf("remove de frequencia no conselho\n");
					buffer = realRemoveLRU(pool->areaLeitura,1);
				}else{//dá preferência para área de frquência
					buffer = realRemoveLRU(pool->areaLeitura,0);
				}
			}
			
			buffer->idPagina = InvalidBlockNumber;
			realInserirBuffer(pool->areaEscrita,2,buffer);

		}else{//Área de leitura deve aumentar
			pool->conselheiroAreaEscrita++;

			pool->areaLeitura->tamanhoMaximo++;
			pool->areaLeitura->livres->tamanhoMaximo++;

			pool->areaEscrita->tamanhoMaximo--;
			pool->areaEscrita->livres->tamanhoMaximo--;

			// //printf("Aumentando área Leitura\n");

			buffer = realRemoveLRU(pool->areaEscrita,2);
			if(buffer == NULL){//remove um buffer livre da escrita
				//não há buffer de escrita livre
				if(pool->conselheiroFrequenciaEscrita < 0){
					// //printf("remove de frequencia no conselho\n");
					buffer = realRemoveLRU(pool->areaEscrita,1);
				}else{//dá preferência para área de frquência
					buffer = realRemoveLRU(pool->areaEscrita,0);
				}
			}
			buffer->idPagina = InvalidBlockNumber;
			realInserirBuffer(pool->areaLeitura,2,buffer);
		}
	}

	deltaLeitura = pool->areaLeitura->tamanhoMaximo - tamanhoAteriorLeitura;
	deltaEscrita = pool->areaEscrita->tamanhoMaximo - tamanhoAteriorEscrita;



	if(deltaLeitura % 2 == 0){
		deltaLeituraRA = deltaLeitura;
		deltaLeituraFA = deltaLeitura;
	}else{
		if(deltaLeitura > 0){
			deltaMaior = deltaLeitura;
			deltaMenor = deltaLeitura - 1;
		}else{
			deltaMaior = deltaLeitura + 1;
			deltaMenor = deltaLeitura;
		}
		if(pool->areaLeitura->RA->tamanhoMaximo > pool->areaLeitura->FA->tamanhoMaximo){
			deltaLeituraRA = deltaMaior;
			deltaLeituraFA = deltaMenor;
		}else{
			deltaLeituraRA = deltaMenor;
			deltaLeituraFA = deltaMaior;
		}
	}


	if(deltaEscrita % 2 == 0){
		deltaEscritaRA = deltaEscrita;
		deltaEscritaFA = deltaEscrita;
	}else{
		if(deltaEscrita > 0){
			deltaMaior = deltaEscrita;
			deltaMenor = deltaEscrita - 1;
		}else{
			deltaMaior = deltaEscrita + 1;
			deltaMenor = deltaEscrita;
		}
		if(pool->areaEscrita->RA->tamanhoMaximo > pool->areaEscrita->FA->tamanhoMaximo){
			deltaEscritaRA = deltaMaior;
			deltaEscritaFA = deltaMenor;
		}else{
			deltaEscritaRA = deltaMenor;
			deltaEscritaFA = deltaMaior;
		}
	}


	pool->areaLeitura->RA->tamanhoMaximo += deltaLeituraRA;
	pool->areaLeitura->FA->tamanhoMaximo += deltaLeituraFA;
	// pool->areaLeitura->GRA->tamanhoMaximo = pool->areaLeitura->RA->tamanhoMaximo;
	// pool->areaLeitura->GFA->tamanhoMaximo = pool->areaLeitura->FA->tamanhoMaximo;

	pool->areaEscrita->RA->tamanhoMaximo += deltaEscritaRA;
	pool->areaEscrita->FA->tamanhoMaximo += deltaEscritaFA;
	// pool->areaEscrita->GRA->tamanhoMaximo = pool->areaEscrita->RA->tamanhoMaximo;
	// pool->areaEscrita->GFA->tamanhoMaximo = pool->areaEscrita->FA->tamanhoMaximo;

	while(pool->conselheiroFrequenciaEscrita != 0 ){
		conselhoListas(pool,1);
	}

	while(pool->conselheiroFrequenciaLeitura != 0 ){
		// //printf("vai seguir conselho %d\n",pool->conselheiroFrequenciaLeitura);
		conselhoListas(pool,0);
	}

	normalizarLista(pool->areaLeitura,0);
	normalizarLista(pool->areaLeitura,1);
	normalizarLista(pool->areaEscrita,0);
	normalizarLista(pool->areaEscrita,1);

}


int atualizarConselheiros(BufferPool *pool, BlockNumber idBlock){
	BlockNumber id = InvalidBlockNumber;
	id = fantasmaRemovePorIdPagina(pool->areaLeitura,0,idBlock);
	if(BlockNumberIsValid(id)){//Estava na lista fantasma de leitura recente.
		// //printf("conselho lr\n");
		// ListaReal *oposta = getList(pool->areaLeitura,1);
		if(pool->areaEscrita->tamanhoMaximo > pool->areaLeitura->tamanhoMaximo / 4){
		// if(pool->areaEscrita->tamanhoAtual < (int)  pool->areaEscrita->tamanhoMaximo/2 ){//aconselha aumentar lista de leitura
			pool->conselheiroAreaEscrita--;
		}
		pool->conselheiroFrequenciaLeitura--;

	}else{
		id = fantasmaRemovePorIdPagina(pool->areaLeitura,1,idBlock);
		if(BlockNumberIsValid(id)){//Estava na lista fantasma de leitura frequente.
			// //printf("conselho lf\n");
			// ListaReal *oposta = getList(pool->areaLeitura,0);
			if(pool->areaEscrita->tamanhoMaximo > pool->areaLeitura->tamanhoMaximo / 4){
			// if(pool->areaEscrita->tamanhoAtual < (int)  pool->areaEscrita->tamanhoMaximo/2){//aconselha aumentar lista de leitura
				pool->conselheiroAreaEscrita--;
			}
			pool->conselheiroFrequenciaLeitura++;
		}else{
			id = fantasmaRemovePorIdPagina(pool->areaEscrita,0,idBlock);
			if(BlockNumberIsValid(id)){//Estava na lista fantasma de escrita recente.
				// //printf("conselho er\n");
				// ListaReal *oposta = getList(pool->areaEscrita,1);
				if(pool->areaLeitura->tamanhoMaximo > pool->areaEscrita->tamanhoMaximo / 4){
				// if(pool->areaLeitura->tamanhoAtual < (int)  pool->areaLeitura->tamanhoMaximo/2 ){//aconselha aumentar lista de escrita
					pool->conselheiroAreaEscrita++;
				}
				pool->conselheiroFrequenciaEscrita--;
			}else{
				// if(opt != 1)
				// 	return -1;
				id = fantasmaRemovePorIdPagina(pool->areaEscrita,1,idBlock);
				if(BlockNumberIsValid(id)){//Estava na lista fantasma de escrita frequente
					// //printf("conselho ef\n");
					// ListaReal *oposta = getList(pool->areaEscrita,0);
					if(pool->areaLeitura->tamanhoMaximo > pool->areaEscrita->tamanhoMaximo / 4){
					// if(pool->areaLeitura->tamanhoAtual < (int)  pool->areaLeitura->tamanhoMaximo/2 ){//aconselha aumentar lista de leitura
						pool->conselheiroAreaEscrita++;
					}
					pool->conselheiroFrequenciaEscrita++;
				}
			}
		}
	}
	return id==idBlock;
}



//Manipulação Lista Real


//Meu Código - FIM