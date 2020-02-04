/* Remote target communications for serial-line targets in custom GDB protocol
   Copyright (C) 1999-2020 Free Software Foundation, Inc.

   This file is part of GDB.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.  */

#ifndef REMOTE_H
#define REMOTE_H

#include <unordered_map>
#include "remote-notif.h"
#include "gdbsupport/btrace-common.h"
#include "gdbsupport/byte-vector.h"
#include "infrun.h"
#include "process-stratum-target.h"
#include "serial.h"

struct target_desc;

#define OPAQUETHREADBYTES 8

/* a 64 bit opaque identifier */
typedef unsigned char threadref[OPAQUETHREADBYTES];

struct gdb_ext_thread_info;
struct threads_listing_context;
typedef int (*rmt_thread_action) (threadref *ref, void *context);
struct protocol_feature;
struct packet_reg;

struct stop_reply;
typedef std::unique_ptr<stop_reply> stop_reply_up;

/* Generic configuration support for packets the stub optionally
   supports.  Allows the user to specify the use of the packet as well
   as allowing GDB to auto-detect support in the remote stub.  */

enum packet_support
  {
    PACKET_SUPPORT_UNKNOWN = 0,
    PACKET_ENABLE,
    PACKET_DISABLE
  };

/* Analyze a packet's return value and update the packet config
   accordingly.  */

enum packet_result
{
  PACKET_ERROR,
  PACKET_OK,
  PACKET_UNKNOWN
};

struct threads_listing_context;

/* Stub vCont actions support.

   Each field is a boolean flag indicating whether the stub reports
   support for the corresponding action.  */

struct vCont_action_support
{
  /* vCont;t */
  bool t = false;

  /* vCont;r */
  bool r = false;

  /* vCont;s */
  bool s = false;

  /* vCont;S */
  bool S = false;
};

/* About this many threadids fit in a packet.  */

#define MAXTHREADLISTRESULTS 32

/* Data for the vFile:pread readahead cache.  */

struct readahead_cache
{
  /* Invalidate the readahead cache.  */
  void invalidate ();

  /* Invalidate the readahead cache if it is holding data for FD.  */
  void invalidate_fd (int fd);

  /* Serve pread from the readahead cache.  Returns number of bytes
     read, or 0 if the request can't be served from the cache.  */
  int pread (int fd, gdb_byte *read_buf, size_t len, ULONGEST offset);

  /* The file descriptor for the file that is being cached.  -1 if the
     cache is invalid.  */
  int fd = -1;

  /* The offset into the file that the cache buffer corresponds
     to.  */
  ULONGEST offset = 0;

  /* The buffer holding the cache contents.  */
  gdb_byte *buf = nullptr;
  /* The buffer's size.  We try to read as much as fits into a packet
     at a time.  */
  size_t bufsize = 0;

  /* Cache hit and miss counters.  */
  ULONGEST hit_count = 0;
  ULONGEST miss_count = 0;
};

/* Description of the remote protocol for a given architecture.  */

struct packet_reg
{
  long offset; /* Offset into G packet.  */
  long regnum; /* GDB's internal register number.  */
  LONGEST pnum; /* Remote protocol register number.  */
  int in_g_packet; /* Always part of G packet.  */
  /* long size in bytes;  == register_size (target_gdbarch (), regnum);
     at present.  */
  /* char *name; == gdbarch_register_name (target_gdbarch (), regnum);
     at present.  */
};

struct remote_arch_state
{
  explicit remote_arch_state (struct gdbarch *gdbarch);

  /* Description of the remote protocol registers.  */
  long sizeof_g_packet;

  /* Description of the remote protocol registers indexed by REGNUM
     (making an array gdbarch_num_regs in size).  */
  std::unique_ptr<packet_reg[]> regs;

  /* This is the size (in chars) of the first response to the ``g''
     packet.  It is used as a heuristic when determining the maximum
     size of memory-read and memory-write packets.  A target will
     typically only reserve a buffer large enough to hold the ``g''
     packet.  The size does not include packet overhead (headers and
     trailers).  */
  long actual_register_packet_size;

  /* This is the maximum size (in chars) of a non read/write packet.
     It is also used as a cap on the size of read/write packets.  */
  long remote_packet_size;
};

/* Description of the remote protocol state for the currently
   connected target.  This is per-target state, and independent of the
   selected architecture.  */

class remote_state
{
public:

  remote_state ();
  ~remote_state ();

  /* Get the remote arch state for GDBARCH.  */
  struct remote_arch_state *get_remote_arch_state (struct gdbarch *gdbarch);

public: /* data */

  /* A buffer to use for incoming packets, and its current size.  The
     buffer is grown dynamically for larger incoming packets.
     Outgoing packets may also be constructed in this buffer.
     The size of the buffer is always at least REMOTE_PACKET_SIZE;
     REMOTE_PACKET_SIZE should be used to limit the length of outgoing
     packets.  */
  gdb::char_vector buf;

  /* True if we're going through initial connection setup (finding out
     about the remote side's threads, relocating symbols, etc.).  */
  bool starting_up = false;

  /* If we negotiated packet size explicitly (and thus can bypass
     heuristics for the largest packet size that will not overflow
     a buffer in the stub), this will be set to that packet size.
     Otherwise zero, meaning to use the guessed size.  */
  long explicit_packet_size = 0;

  /* remote_wait is normally called when the target is running and
     waits for a stop reply packet.  But sometimes we need to call it
     when the target is already stopped.  We can send a "?" packet
     and have remote_wait read the response.  Or, if we already have
     the response, we can stash it in BUF and tell remote_wait to
     skip calling getpkt.  This flag is set when BUF contains a
     stop reply packet and the target is not waiting.  */
  int cached_wait_status = 0;

  /* True, if in no ack mode.  That is, neither GDB nor the stub will
     expect acks from each other.  The connection is assumed to be
     reliable.  */
  bool noack_mode = false;

  /* True if we're connected in extended remote mode.  */
  bool extended = false;

  /* True if we resumed the target and we're waiting for the target to
     stop.  In the mean time, we can't start another command/query.
     The remote server wouldn't be ready to process it, so we'd
     timeout waiting for a reply that would never come and eventually
     we'd close the connection.  This can happen in asynchronous mode
     because we allow GDB commands while the target is running.  */
  bool waiting_for_stop_reply = false;

  /* The status of the stub support for the various vCont actions.  */
  vCont_action_support supports_vCont;
  /* Whether vCont support was probed already.  This is a workaround
     until packet_support is per-connection.  */
  bool supports_vCont_probed;

  /* True if the user has pressed Ctrl-C, but the target hasn't
     responded to that.  */
  bool ctrlc_pending_p = false;

  /* True if we saw a Ctrl-C while reading or writing from/to the
     remote descriptor.  At that point it is not safe to send a remote
     interrupt packet, so we instead remember we saw the Ctrl-C and
     process it once we're done with sending/receiving the current
     packet, which should be shortly.  If however that takes too long,
     and the user presses Ctrl-C again, we offer to disconnect.  */
  bool got_ctrlc_during_io = false;

  /* Descriptor for I/O to remote machine.  Initialize it to NULL so that
     remote_open knows that we don't have a file open when the program
     starts.  */
  struct serial *remote_desc = nullptr;

  /* These are the threads which we last sent to the remote system.  The
     TID member will be -1 for all or -2 for not sent yet.  */
  ptid_t general_thread = null_ptid;
  ptid_t continue_thread = null_ptid;

  /* This is the traceframe which we last selected on the remote system.
     It will be -1 if no traceframe is selected.  */
  int remote_traceframe_number = -1;

  char *last_pass_packet = nullptr;

  /* The last QProgramSignals packet sent to the target.  We bypass
     sending a new program signals list down to the target if the new
     packet is exactly the same as the last we sent.  IOW, we only let
     the target know about program signals list changes.  */
  char *last_program_signals_packet = nullptr;

  gdb_signal last_sent_signal = GDB_SIGNAL_0;

  bool last_sent_step = false;

  /* The execution direction of the last resume we got.  */
  exec_direction_kind last_resume_exec_dir = EXEC_FORWARD;

  char *finished_object = nullptr;
  char *finished_annex = nullptr;
  ULONGEST finished_offset = 0;

  /* Should we try the 'ThreadInfo' query packet?

     This variable (NOT available to the user: auto-detect only!)
     determines whether GDB will use the new, simpler "ThreadInfo"
     query or the older, more complex syntax for thread queries.
     This is an auto-detect variable (set to true at each connect,
     and set to false when the target fails to recognize it).  */
  bool use_threadinfo_query = false;
  bool use_threadextra_query = false;

  threadref echo_nextthread {};
  threadref nextthread {};
  threadref resultthreadlist[MAXTHREADLISTRESULTS] {};

  /* The state of remote notification.  */
  struct remote_notif_state *notif_state = nullptr;

  /* The branch trace configuration.  */
  struct btrace_config btrace_config {};

  /* The argument to the last "vFile:setfs:" packet we sent, used
     to avoid sending repeated unnecessary "vFile:setfs:" packets.
     Initialized to -1 to indicate that no "vFile:setfs:" packet
     has yet been sent.  */
  int fs_pid = -1;

  /* A readahead cache for vFile:pread.  Often, reading a binary
     involves a sequence of small reads.  E.g., when parsing an ELF
     file.  A readahead cache helps mostly the case of remote
     debugging on a connection with higher latency, due to the
     request/reply nature of the RSP.  We only cache data for a single
     file descriptor at a time.  */
  struct readahead_cache readahead_cache;

  /* The list of already fetched and acknowledged stop events.  This
     queue is used for notification Stop, and other notifications
     don't need queue for their events, because the notification
     events of Stop can't be consumed immediately, so that events
     should be queued first, and be consumed by remote_wait_{ns,as}
     one per time.  Other notifications can consume their events
     immediately, so queue is not needed for them.  */
  std::vector<stop_reply_up> stop_reply_queue;

  /* Asynchronous signal handle registered as event loop source for
     when we have pending events ready to be passed to the core.  */
  struct async_event_handler *remote_async_inferior_event_token = nullptr;

  /* FIXME: cagney/1999-09-23: Even though getpkt was called with
     ``forever'' still use the normal timeout mechanism.  This is
     currently used by the ASYNC code to guarentee that target reads
     during the initial connect always time-out.  Once getpkt has been
     modified to return a timeout indication and, in turn
     remote_wait()/wait_for_inferior() have gained a timeout parameter
     this can go away.  */
  int wait_forever_enabled_p = 1;

private:
  /* Mapping of remote protocol data for each gdbarch.  Usually there
     is only one entry here, though we may see more with stubs that
     support multi-process.  */
  std::unordered_map<struct gdbarch *, remote_arch_state>
    m_arch_states;
};

class remote_target : public process_stratum_target
{
public:
  remote_target () = default;
  ~remote_target () override;

  const target_info &info () const override;

  const char *connection_string () override;

  thread_control_capabilities get_thread_control_capabilities () override
  { return tc_schedlock; }

  /* Open a remote connection.  */
  static void open (const char *, int);

  void close () override;

  void detach (inferior *, int) override;
  void disconnect (const char *, int) override;

  void commit_resume () override;
  void resume (ptid_t, int, enum gdb_signal) override;
  ptid_t wait (ptid_t, struct target_waitstatus *, int) override;

  void fetch_registers (struct regcache *, int) override;
  void store_registers (struct regcache *, int) override;
  void prepare_to_store (struct regcache *) override;

  void files_info () override;

  int insert_breakpoint (struct gdbarch *, struct bp_target_info *) override;

  int remove_breakpoint (struct gdbarch *, struct bp_target_info *,
			 enum remove_bp_reason) override;


  bool stopped_by_sw_breakpoint () override;
  bool supports_stopped_by_sw_breakpoint () override;

  bool stopped_by_hw_breakpoint () override;

  bool supports_stopped_by_hw_breakpoint () override;

  bool stopped_by_watchpoint () override;

  bool stopped_data_address (CORE_ADDR *) override;

  bool watchpoint_addr_within_range (CORE_ADDR, CORE_ADDR, int) override;

  int can_use_hw_breakpoint (enum bptype, int, int) override;

  int insert_hw_breakpoint (struct gdbarch *, struct bp_target_info *) override;

  int remove_hw_breakpoint (struct gdbarch *, struct bp_target_info *) override;

  int region_ok_for_hw_watchpoint (CORE_ADDR, int) override;

  int insert_watchpoint (CORE_ADDR, int, enum target_hw_bp_type,
			 struct expression *) override;

  int remove_watchpoint (CORE_ADDR, int, enum target_hw_bp_type,
			 struct expression *) override;

  void kill () override;

  void load (const char *, int) override;

  void mourn_inferior () override;

  void pass_signals (gdb::array_view<const unsigned char>) override;

  int set_syscall_catchpoint (int, bool, int,
			      gdb::array_view<const int>) override;

  void program_signals (gdb::array_view<const unsigned char>) override;

  bool thread_alive (ptid_t ptid) override;

  const char *thread_name (struct thread_info *) override;

  void update_thread_list () override;

  std::string pid_to_str (ptid_t) override;

  const char *extra_thread_info (struct thread_info *) override;

  ptid_t get_ada_task_ptid (long lwp, long thread) override;

  thread_info *thread_handle_to_thread_info (const gdb_byte *thread_handle,
					     int handle_len,
					     inferior *inf) override;

  gdb::byte_vector thread_info_to_thread_handle (struct thread_info *tp)
						 override;

  void stop (ptid_t) override;

  void interrupt () override;

  void pass_ctrlc () override;

  enum target_xfer_status xfer_partial (enum target_object object,
					const char *annex,
					gdb_byte *readbuf,
					const gdb_byte *writebuf,
					ULONGEST offset, ULONGEST len,
					ULONGEST *xfered_len) override;

  ULONGEST get_memory_xfer_limit () override;

  void rcmd (const char *command, struct ui_file *output) override;

  char *pid_to_exec_file (int pid) override;

  void log_command (const char *cmd) override
  {
    serial_log_command (this, cmd);
  }

  CORE_ADDR get_thread_local_address (ptid_t ptid,
				      CORE_ADDR load_module_addr,
				      CORE_ADDR offset) override;

  bool can_execute_reverse () override;

  std::vector<mem_region> memory_map () override;

  void flash_erase (ULONGEST address, LONGEST length) override;

  void flash_done () override;

  const struct target_desc *read_description () override;

  int search_memory (CORE_ADDR start_addr, ULONGEST search_space_len,
		     const gdb_byte *pattern, ULONGEST pattern_len,
		     CORE_ADDR *found_addrp) override;

  bool can_async_p () override;

  bool is_async_p () override;

  void async (int) override;

  int async_wait_fd () override;

  void thread_events (int) override;

  int can_do_single_step () override;

  void terminal_inferior () override;

  void terminal_ours () override;

  bool supports_non_stop () override;

  bool supports_multi_process () override;

  bool supports_disable_randomization () override;

  bool filesystem_is_local () override;


  int fileio_open (struct inferior *inf, const char *filename,
		   int flags, int mode, int warn_if_slow,
		   int *target_errno) override;

  int fileio_pwrite (int fd, const gdb_byte *write_buf, int len,
		     ULONGEST offset, int *target_errno) override;

  int fileio_pread (int fd, gdb_byte *read_buf, int len,
		    ULONGEST offset, int *target_errno) override;

  int fileio_fstat (int fd, struct stat *sb, int *target_errno) override;

  int fileio_close (int fd, int *target_errno) override;

  int fileio_unlink (struct inferior *inf,
		     const char *filename,
		     int *target_errno) override;

  gdb::optional<std::string>
    fileio_readlink (struct inferior *inf,
		     const char *filename,
		     int *target_errno) override;

  bool supports_enable_disable_tracepoint () override;

  bool supports_string_tracing () override;

  bool supports_evaluation_of_breakpoint_conditions () override;

  bool can_run_breakpoint_commands () override;

  void trace_init () override;

  void download_tracepoint (struct bp_location *location) override;

  bool can_download_tracepoint () override;

  void download_trace_state_variable (const trace_state_variable &tsv) override;

  void enable_tracepoint (struct bp_location *location) override;

  void disable_tracepoint (struct bp_location *location) override;

  void trace_set_readonly_regions () override;

  void trace_start () override;

  int get_trace_status (struct trace_status *ts) override;

  void get_tracepoint_status (struct breakpoint *tp, struct uploaded_tp *utp)
    override;

  void trace_stop () override;

  int trace_find (enum trace_find_type type, int num,
		  CORE_ADDR addr1, CORE_ADDR addr2, int *tpp) override;

  bool get_trace_state_variable_value (int tsv, LONGEST *val) override;

  int save_trace_data (const char *filename) override;

  int upload_tracepoints (struct uploaded_tp **utpp) override;

  int upload_trace_state_variables (struct uploaded_tsv **utsvp) override;

  LONGEST get_raw_trace_data (gdb_byte *buf, ULONGEST offset, LONGEST len) override;

  int get_min_fast_tracepoint_insn_len () override;

  void set_disconnected_tracing (int val) override;

  void set_circular_trace_buffer (int val) override;

  void set_trace_buffer_size (LONGEST val) override;

  bool set_trace_notes (const char *user, const char *notes,
			const char *stopnotes) override;

  int core_of_thread (ptid_t ptid) override;

  int verify_memory (const gdb_byte *data,
		     CORE_ADDR memaddr, ULONGEST size) override;


  bool get_tib_address (ptid_t ptid, CORE_ADDR *addr) override;

  void set_permissions () override;

  bool static_tracepoint_marker_at (CORE_ADDR,
				    struct static_tracepoint_marker *marker)
    override;

  std::vector<static_tracepoint_marker>
    static_tracepoint_markers_by_strid (const char *id) override;

  traceframe_info_up traceframe_info () override;

  bool use_agent (bool use) override;
  bool can_use_agent () override;

  struct btrace_target_info *enable_btrace (ptid_t ptid,
					    const struct btrace_config *conf) override;

  void disable_btrace (struct btrace_target_info *tinfo) override;

  void teardown_btrace (struct btrace_target_info *tinfo) override;

  enum btrace_error read_btrace (struct btrace_data *data,
				 struct btrace_target_info *btinfo,
				 enum btrace_read_type type) override;

  const struct btrace_config *btrace_conf (const struct btrace_target_info *) override;
  bool augmented_libraries_svr4_read () override;
  int follow_fork (int, int) override;
  void follow_exec (struct inferior *, const char *) override;
  int insert_fork_catchpoint (int) override;
  int remove_fork_catchpoint (int) override;
  int insert_vfork_catchpoint (int) override;
  int remove_vfork_catchpoint (int) override;
  int insert_exec_catchpoint (int) override;
  int remove_exec_catchpoint (int) override;
  enum exec_direction_kind execution_direction () override;

public: /* Remote specific methods.  */

  void remote_download_command_source (int num, ULONGEST addr,
				       struct command_line *cmds);

  void remote_file_put (const char *local_file, const char *remote_file,
			int from_tty);
  void remote_file_get (const char *remote_file, const char *local_file,
			int from_tty);
  void remote_file_delete (const char *remote_file, int from_tty);

  int remote_hostio_pread (int fd, gdb_byte *read_buf, int len,
			   ULONGEST offset, int *remote_errno);
  int remote_hostio_pwrite (int fd, const gdb_byte *write_buf, int len,
			    ULONGEST offset, int *remote_errno);
  int remote_hostio_pread_vFile (int fd, gdb_byte *read_buf, int len,
				 ULONGEST offset, int *remote_errno);

  int remote_hostio_send_command (int command_bytes, int which_packet,
				  int *remote_errno, char **attachment,
				  int *attachment_len);
  int remote_hostio_set_filesystem (struct inferior *inf,
				    int *remote_errno);
  /* We should get rid of this and use fileio_open directly.  */
  int remote_hostio_open (struct inferior *inf, const char *filename,
			  int flags, int mode, int warn_if_slow,
			  int *remote_errno);
  int remote_hostio_close (int fd, int *remote_errno);

  int remote_hostio_unlink (inferior *inf, const char *filename,
			    int *remote_errno);

  struct remote_state *get_remote_state ();

  long get_remote_packet_size (void);
  long get_memory_packet_size (struct memory_packet_config *config);

  long get_memory_write_packet_size ();
  long get_memory_read_packet_size ();

  char *append_pending_thread_resumptions (char *p, char *endp,
					   ptid_t ptid);
  void start_remote (int from_tty, int extended_p);
  void remote_detach_1 (struct inferior *inf, int from_tty);

  char *append_resumption (char *p, char *endp,
			   ptid_t ptid, int step, gdb_signal siggnal);
  int remote_resume_with_vcont (ptid_t ptid, int step,
				gdb_signal siggnal);

  void add_current_inferior_and_thread (char *wait_status);

  ptid_t wait_ns (ptid_t ptid, struct target_waitstatus *status,
		  int options);
  ptid_t wait_as (ptid_t ptid, target_waitstatus *status,
		  int options);

  ptid_t process_stop_reply (struct stop_reply *stop_reply,
			     target_waitstatus *status);

  void remote_notice_new_inferior (ptid_t currthread, int executing);

  void process_initial_stop_replies (int from_tty);

  thread_info *remote_add_thread (ptid_t ptid, bool running, bool executing);

  void btrace_sync_conf (const btrace_config *conf);

  void remote_btrace_maybe_reopen ();

  void remove_new_fork_children (threads_listing_context *context);
  void kill_new_fork_children (int pid);
  void discard_pending_stop_replies (struct inferior *inf);
  int stop_reply_queue_length ();

  void check_pending_events_prevent_wildcard_vcont
    (int *may_global_wildcard_vcont);

  void discard_pending_stop_replies_in_queue ();
  struct stop_reply *remote_notif_remove_queued_reply (ptid_t ptid);
  struct stop_reply *queued_stop_reply (ptid_t ptid);
  int peek_stop_reply (ptid_t ptid);
  void remote_parse_stop_reply (const char *buf, stop_reply *event);

  void remote_stop_ns (ptid_t ptid);
  void remote_interrupt_as ();
  void remote_interrupt_ns ();

  char *remote_get_noisy_reply ();
  int remote_query_attached (int pid);
  inferior *remote_add_inferior (bool fake_pid_p, int pid, int attached,
				 int try_open_exec);

  ptid_t remote_current_thread (ptid_t oldpid);
  ptid_t get_current_thread (char *wait_status);

  void set_thread (ptid_t ptid, int gen);
  void set_general_thread (ptid_t ptid);
  void set_continue_thread (ptid_t ptid);
  void set_general_process ();

  char *write_ptid (char *buf, const char *endbuf, ptid_t ptid);

  int remote_unpack_thread_info_response (char *pkt, threadref *expectedref,
					  gdb_ext_thread_info *info);
  int remote_get_threadinfo (threadref *threadid, int fieldset,
			     gdb_ext_thread_info *info);

  int parse_threadlist_response (char *pkt, int result_limit,
				 threadref *original_echo,
				 threadref *resultlist,
				 int *doneflag);
  int remote_get_threadlist (int startflag, threadref *nextthread,
			     int result_limit, int *done, int *result_count,
			     threadref *threadlist);

  int remote_threadlist_iterator (rmt_thread_action stepfunction,
				  void *context, int looplimit);

  int remote_get_threads_with_ql (threads_listing_context *context);
  int remote_get_threads_with_qxfer (threads_listing_context *context);
  int remote_get_threads_with_qthreadinfo (threads_listing_context *context);

  void extended_remote_restart ();

  void get_offsets ();

  void remote_check_symbols ();

  void remote_supported_packet (const struct protocol_feature *feature,
				enum packet_support support,
				const char *argument);

  void remote_query_supported ();

  void remote_packet_size (const protocol_feature *feature,
			   packet_support support, const char *value);

  void remote_serial_quit_handler ();

  void remote_detach_pid (int pid);

  void remote_vcont_probe ();

  void remote_resume_with_hc (ptid_t ptid, int step,
			      gdb_signal siggnal);

  void send_interrupt_sequence ();
  void interrupt_query ();

  void remote_notif_get_pending_events (notif_client *nc);

  int fetch_register_using_p (struct regcache *regcache,
			      packet_reg *reg);
  int send_g_packet ();
  void process_g_packet (struct regcache *regcache);
  void fetch_registers_using_g (struct regcache *regcache);
  int store_register_using_P (const struct regcache *regcache,
			      packet_reg *reg);
  void store_registers_using_G (const struct regcache *regcache);

  void set_remote_traceframe ();

  void check_binary_download (CORE_ADDR addr);

  target_xfer_status remote_write_bytes_aux (const char *header,
					     CORE_ADDR memaddr,
					     const gdb_byte *myaddr,
					     ULONGEST len_units,
					     int unit_size,
					     ULONGEST *xfered_len_units,
					     char packet_format,
					     int use_length);

  target_xfer_status remote_write_bytes (CORE_ADDR memaddr,
					 const gdb_byte *myaddr, ULONGEST len,
					 int unit_size, ULONGEST *xfered_len);

  target_xfer_status remote_read_bytes_1 (CORE_ADDR memaddr, gdb_byte *myaddr,
					  ULONGEST len_units,
					  int unit_size, ULONGEST *xfered_len_units);

  target_xfer_status remote_xfer_live_readonly_partial (gdb_byte *readbuf,
							ULONGEST memaddr,
							ULONGEST len,
							int unit_size,
							ULONGEST *xfered_len);

  target_xfer_status remote_read_bytes (CORE_ADDR memaddr,
					gdb_byte *myaddr, ULONGEST len,
					int unit_size,
					ULONGEST *xfered_len);

  packet_result remote_send_printf (const char *format, ...)
    ATTRIBUTE_PRINTF (2, 3);

  target_xfer_status remote_flash_write (ULONGEST address,
					 ULONGEST length, ULONGEST *xfered_len,
					 const gdb_byte *data);

  int readchar (int timeout);

  void remote_serial_write (const char *str, int len);

  int putpkt (const char *buf);
  int putpkt_binary (const char *buf, int cnt);

  int putpkt (const gdb::char_vector &buf)
  {
    return putpkt (buf.data ());
  }

  void skip_frame ();
  long read_frame (gdb::char_vector *buf_p);
  void getpkt (gdb::char_vector *buf, int forever);
  int getpkt_or_notif_sane_1 (gdb::char_vector *buf, int forever,
			      int expecting_notif, int *is_notif);
  int getpkt_sane (gdb::char_vector *buf, int forever);
  int getpkt_or_notif_sane (gdb::char_vector *buf, int forever,
			    int *is_notif);
  int remote_vkill (int pid);
  void remote_kill_k ();

  void extended_remote_disable_randomization (int val);
  int extended_remote_run (const std::string &args);

  void send_environment_packet (const char *action,
				const char *packet,
				const char *value);

  void extended_remote_environment_support ();
  void extended_remote_set_inferior_cwd ();

  target_xfer_status remote_write_qxfer (const char *object_name,
					 const char *annex,
					 const gdb_byte *writebuf,
					 ULONGEST offset, LONGEST len,
					 ULONGEST *xfered_len,
					 struct packet_config *packet);

  target_xfer_status remote_read_qxfer (const char *object_name,
					const char *annex,
					gdb_byte *readbuf, ULONGEST offset,
					LONGEST len,
					ULONGEST *xfered_len,
					struct packet_config *packet);

  void push_stop_reply (struct stop_reply *new_event);

  bool vcont_r_supported ();

  void packet_command (const char *args, int from_tty);

protected:

  static void open_1 (const char *name, int from_tty, int extended_p,
		      remote_target *(*create) ());

private: /* data fields */

  /* The remote state.  Don't reference this directly.  Use the
     get_remote_state method instead.  */
  remote_state m_remote_state;
};




/* Read a packet from the remote machine, with error checking, and
   store it in *BUF.  Resize *BUF using xrealloc if necessary to hold
   the result, and update *SIZEOF_BUF.  If FOREVER, wait forever
   rather than timing out; this is used (in synchronous mode) to wait
   for a target that is is executing user code to stop.  */

extern void getpkt (remote_target *remote,
		    char **buf, long *sizeof_buf, int forever);

/* Send a packet to the remote machine, with error checking.  The data
   of the packet is in BUF.  The string in BUF can be at most PBUFSIZ
   - 5 to account for the $, # and checksum, and for a possible /0 if
   we are debugging (remote_debug) and want to print the sent packet
   as a string.  */

extern int putpkt (remote_target *remote, const char *buf);

void register_remote_g_packet_guess (struct gdbarch *gdbarch, int bytes,
				     const struct target_desc *tdesc);
void register_remote_support_xml (const char *);

void remote_file_put (const char *local_file, const char *remote_file,
		      int from_tty);
void remote_file_get (const char *remote_file, const char *local_file,
		      int from_tty);
void remote_file_delete (const char *remote_file, int from_tty);

extern int remote_register_number_and_offset (struct gdbarch *gdbarch,
					      int regnum, int *pnum,
					      int *poffset);

extern void remote_notif_get_pending_events (remote_target *remote,
					     struct notif_client *np);
#endif
