/* Copyright (c) 2014, 2017, Oracle and/or its affiliates. All rights reserved.

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA */

#ifndef _PROCESS_LAUNCHER_H_
#define _PROCESS_LAUNCHER_H_

#ifdef WIN32
#  define _CRT_SECURE_NO_WARNINGS 1
#  ifdef UNICODE
#    #undef UNICODE
#  endif
#  include <windows.h>
#else
#  include <unistd.h>
#endif
#include <stdint.h>

// Launches a process as child of current process and exposes the stdin & stdout of the child process
// (implemented thru pipelines) so the client of this class can read from the child's stdout and write to the child's stdin.
// For usage, see unit tests.
//
class ProcessLauncher {
public:

  /**
   * Creates a new process and launch it.
   * Argument 'args' must have a last entry that is NULL.
   * If redirect_stderr is true, the child's stderr is redirected to the same stream than child's stdout.
   */
  ProcessLauncher(const char *pcmd_line, const char ** pargs, bool predirect_stderr = true) : is_alive(false) {
    this->cmd_line = pcmd_line;
    this->args = pargs;
    this->redirect_stderr = predirect_stderr;
  }

  ~ProcessLauncher() { if (is_alive) close(); }

  /** Launches the child process, and makes pipes available for read/write. */
  void start();

  /**
   * Read up to a 'count' bytes from the stdout of the child process.
   * This method blocks until the amount of bytes is read or specified timeout
   * expires.
   * @param buf already allocated buffer where the read data will be stored.
   * @param count the maximum amount of bytes to read.
   * @param timeout_ms timeout (in milliseconds) for the read to complete
   * @return the real number of bytes read.
   * Returns an shcore::Exception in case of error when reading.
   */
  int read(char *buf, size_t count, unsigned timeout_ms);

  /**
   * Writes several butes into stdin of child process.
   * Returns an shcore::Exception in case of error when writing.
   */
  int write(const char *buf, size_t count);

  /**
   * Kills the child process.
   */
  void kill();

  /**
   * Returns the child process handle.
   * In Linux this needs to be cast to pid_t, in Windows to cast to HANDLE.
   */
  uint64_t get_pid();

  /**
   * Wait for the child process to exists and returns its exit code.
   * If the child process is already dead, wait() it just returns.
   * Returns the exit code of the process.
   */
  int wait(unsigned int timeout_ms = 1000);

  /**
  * Returns the file descriptor write handle (to write child's stdin).
  * In Linux this needs to be cast to int, in Windows to cast to HANDLE.
  */
  uint64_t get_fd_write();

  /**
  * Returns the file descriptor read handle (to read child's stdout).
  * In Linux this needs to be cast to int, in Windows to cast to HANDLE.
  */
  uint64_t get_fd_read();

private:
  /**
   * Throws an exception with the specified message, if msg == NULL, the exception's message is specific of the platform error.
   * (errno in Linux / GetLastError in Windows).
   */
  void report_error(const char *msg, const char* prefix = "");
  /** Closes child process */
  void close();

  const char *cmd_line;
  const char **args;
  bool is_alive;
#ifdef WIN32
  HANDLE child_in_rd;
  HANDLE child_in_wr;
  HANDLE child_out_rd;
  HANDLE child_out_wr;
  PROCESS_INFORMATION pi;
  STARTUPINFO si;
#else
  pid_t childpid;
  int fd_in[2];
  int fd_out[2];
#endif
  bool redirect_stderr;
};

#endif // _PROCESS_LAUNCHER_H_
