/**
 * XETH side-band channel.
 *
 * SPDX-License-Identifier: GPL-2.0
 * Copyright(c) 2018-2019 Platina Systems, Inc.
 *
 * Contact Information:
 * sw@platina.com
 * Platina Systems, 3180 Del La Cruz Blvd, Santa Clara, CA 95054
 */

static void xeth_sb_service(struct socket *conn)
{
	struct task_struct *sbrx;

	sbrx = xeth_sbrx_fork(conn);
	if (!sbrx)
		return;
	xeth_sbtx_service(conn);
	if (xeth_flag(sbrx_task)) {
		kthread_stop(sbrx);
		while (xeth_flag(sbrx_task)) ;
	}
	xeth_upper_all_carrier_off();
}

static int xeth_sb_task(void *data)
{
	const int backlog = 3;
	struct socket *ln = NULL, *conn = NULL;
	struct sockaddr_un addr;
	char name[TASK_COMM_LEN];
	int n, err;

	xeth_flag_set(sb_task);
	get_task_comm(name, current);
	allow_signal(SIGKILL);
	// set_current_state(TASK_INTERRUPTIBLE);

	memset(&addr, 0, sizeof(struct sockaddr_un));
	addr.sun_family = AF_UNIX;
	/* Note: This is an abstract namespace w/ addr.sun_path[0] == 0 */
	n = sizeof(sa_family_t) + 1 +
		scnprintf(addr.sun_path+1, UNIX_PATH_MAX-1, "%s", xeth_name);

	err = xeth_debug_err(sock_create_kern(current->nsproxy->net_ns,
					      AF_UNIX, SOCK_SEQPACKET, 0,
					      &ln));
	if (err)
		goto xeth_sb_task_egress;
	SOCK_INODE(ln)->i_mode &= ~(S_IRWXG | S_IRWXO);
	err = xeth_debug_err(kernel_bind(ln, (struct sockaddr *)&addr, n));
	if (err)
		goto xeth_sb_task_egress;
	err = xeth_debug_err(kernel_listen(ln, backlog));
	if (err)
		goto xeth_sb_task_egress;
	xeth_flag_set(sb_listen);
	while(!err && !kthread_should_stop() && !signal_pending(current)) {
		err = kernel_accept(ln, &conn, O_NONBLOCK);
		if (err == -EAGAIN) {
			err = 0;
			msleep_interruptible(100);
			schedule();
			continue;
		}
		if (!err) {
			xeth_upper_all_reset_stats();
			xeth_flag_set(sb_connected);
			xeth_sb_service(conn);
			sock_release(conn);
			xeth_flag_clear(sb_connected);
		}
	}
	rcu_barrier();
	xeth_flag_clear(sb_listen);
xeth_sb_task_egress:
	if (ln)
		sock_release(ln);
	xeth_flag_clear(sb_task);
	xeth_debug_err(err);
	return err;
}

struct task_struct __init *xeth_sb_start(void)
{
	return xeth_debug_ptr_err(kthread_run(xeth_sb_task, NULL,
					      "%s", xeth_name));
}
