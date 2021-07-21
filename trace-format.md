Described in this file is the version 1.0 of the sqlite block trace format.

# Format overview

Two tables in a sqlite file created using rvnmetadata (and the correct format type).

The two tables are described in the following sections.

## Blocks

The list of all basic blocks executed in the trace, in order of the first execution of the block.

The first block in the table must be a special block with instruction_data equal to "interrupt", that is referenced by
the execution table when non-instructions such  as interrupts and faults are executed.

### Fields

-  "pc int8 not null," -- The address of the first instruction executed in the block
- "instruction_data blob not null," -- A blob of the bytes of all the instructions in the block
- "instruction_count int2 not null," -- The number of instructions in the block
- "mode int1 not null" -- The execution mode (64, 32 bits or 16 bits, x86 only at the moment).
  Values can be found in the `ExecutionMode` enum in `block_writer.h`.

## Execution

The sequential list of all the execution events in the trace.
In the context of this library, an execution event corresponds to the execution of an entire basic
block, or of part of a basic block.

### Fields

- "transition_id int8 PRIMARY KEY not null," -- The transition id of the first transition **that is after**
  the execution of this block.
- "block_id int4 not null" -- The rowid of the executed block

### Implementation detail

This table is "WITHOUT_ROWID" to disable the automatic rowid column added by sqlite. This spares us
an explicit index on `transition_id`, which benefits the size of the database and the speed of access.

It is OK to do so, because two execution events at the same transition is always a bug.
