import { Node } from "godot";
import { reportTestCompleted, reportTestFailure } from "../test-status";

/**
 * 定时器时钟用例：JS 定时器轮不看表，只累加每次推进进来的毫秒数。时钟走得准不准、
 * 武装定时器时用的是不是当前的钟，从里面看不出来，只能从外面量。
 *
 * 守住两件事：
 *
 * 1. 首帧猛推。时钟第一次推进之前不能把引擎启动时长灌进去，否则启动阶段注册的
 *    `setTimeout(fn, 50)` 会在第一帧立刻触发（墙上时间才 0ms）。
 * 2. 卡顿之后武装的定时器不能提前响。主线程被占住期间时钟不推进，欠下的那段真实时间
 *    会在下一次推进时补上；如果武装定时器时不把它补进首次延时，到期时间就是拿一个落后的
 *    钟算出来的，欠的那段会被算成「已经等过了」，定时器当场就响。典型后果：测试看门狗
 *    明明只过了 2 秒就报「超时 5 秒」。
 *
 * 为什么单独起进程、把本场景当主场景跑，而不是挂进 `start.ts` 的场景清单：首帧猛推只在
 * 进程的第一帧看得见，等 `start.ts` 把场景加载进来时早就过去了。跑法见
 * `run-runtime-matrix.mts` 里标了 `timer clock` 的那一步。
 *
 * 定时器停摆时 `await` 根本不返回，所以整体挂一个墙上时间兜底，到点直接判负退出。
 *
 * 改完本文件要重跑一次 `tests/project` 的构建（`pnpm build`，或直接 `node_modules/.bin/tsc`）。
 * 引擎读的是 `tests/project/.godot/GodotJS/` 下的编译产物，不重编就会拿着旧产物跑，
 * 用例会“通过”但其实跑的不是你改的那份。
 */

/** 整个用例的时间预算，超了就判负。 */
const BUDGET_MS = 20000;

/** 模拟主线程被占住的时长。要明显大于被测定时器的延时，才能暴露「延时被欠账吃掉」。 */
const STALL_MS = 1200;

/** 定时器轮的最小粒度是 10ms（`kJiffies`），判定下界要留出这段量化误差。 */
const JIFFIE_SLACK_MS = 10;

/** 判定上界留的余量：一帧的墙上时间加上轮子粒度，再放宽一点。 */
const LATENESS_BUDGET_MS = 150;

function check(label: string, ok: boolean, detail: string): void {
	console.assert(ok, `timer-clock: ${label} —— ${detail}`);
	if (!ok) {
		throw new Error(`timer-clock: ${label} —— ${detail}`);
	}
}

/** 把主线程占住 p_ms。占住期间引擎不跑帧，JS 定时器时钟也就停在原地。 */
function busy(p_ms: number): void {
	const stop = Date.now() + p_ms;
	while (Date.now() < stop) {
		// 空转
	}
}

export default class TimerClock extends Node {
	private startedAtMs = Date.now();
	private finished = false;

	_ready(): void {
		setTimeout(() => {
			this.finish(`超过 ${String(BUDGET_MS)}ms 没跑完，定时器没有触发`);
		}, BUDGET_MS);
		void this.run();
	}

	private elapsed(): number {
		return Date.now() - this.startedAtMs;
	}

	private async run(): Promise<void> {
		try {
			// 启动本身已经耗掉 50ms 以上时，首帧那条断言没有意义，跳过
			if (this.elapsed() < 50) {
				const took = await this.timed(() => new Promise<void>((resolve) => setTimeout(resolve, 50)));
				check(
					"首帧不能把引擎启动时长灌进定时器轮",
					took >= 50 - JIFFIE_SLACK_MS,
					`setTimeout(50ms) 只等了 ${String(took)}ms 就触发了`
				);
			}

			await this.checkDelay(60);
			await this.checkDelay(300);
			await this.checkInterval();
			await this.checkSetImmediate();
			await this.checkAfterStall();

			reportTestCompleted();
		} catch (error) {
			this.finish(error);
			return;
		}
		this.get_tree()?.quit();
	}

	/** 量一段 await 的真实耗时。裁判用 Date.now()，与被测的定时器轮无关。 */
	private async timed(body: () => Promise<void>): Promise<number> {
		const from = Date.now();
		await body();
		return Date.now() - from;
	}

	private async checkDelay(ms: number): Promise<void> {
		const minMs = ms - JIFFIE_SLACK_MS;
		const maxMs = ms + LATENESS_BUDGET_MS;
		const took = await this.timed(() => new Promise<void>((resolve) => setTimeout(resolve, ms)));
		check(`setTimeout(${String(ms)}ms)`, took >= minMs && took <= maxMs, `实际 ${String(took)}ms，期望 ${String(minMs)}~${String(maxMs)}ms`);
	}

	private async checkInterval(): Promise<void> {
		let count = 0;
		const interval = setInterval(() => {
			count += 1;
		}, 40);
		await this.timed(() => new Promise<void>((resolve) => setTimeout(resolve, 200)));
		clearInterval(interval);

		// 40ms 的间隔实际落在 40~50ms（轮子粒度 10ms），200ms 至少该有 4 次
		const fired = count;
		check("setInterval(40ms) 按次数推进", fired >= 4 && fired <= 12, `200ms 内只触发了 ${String(fired)} 次`);

		await this.timed(() => new Promise<void>((resolve) => setTimeout(resolve, 100)));
		check("clearInterval 之后不再触发", count === fired, `清掉后又触发了 ${String(count - fired)} 次`);
	}

	/**
	 * 卡顿之后武装的定时器，必须等满自己的延时才响。
	 * 判据的下界是关键：时钟落后时，这些定时器会在卡顿结束的那一帧当场触发（实测 0ms）。
	 */
	private async checkAfterStall(): Promise<void> {
		// setTimeout：卡顿结束之后再武装
		busy(STALL_MS);
		await this.checkDelay(500);

		// 看门狗形状：长延时的定时器同样不能被欠账吃掉
		busy(STALL_MS);
		await this.checkDelay(2500);

		// setInterval 的首次延时也要补（后续重排发生在刚推进完时钟之后，本来就不落后）
		busy(STALL_MS);
		const first = await this.timed(() => new Promise<void>((resolve) => {
			const interval = setInterval(() => {
				clearInterval(interval);
				resolve();
			}, 500);
		}));
		check(
			`卡顿 ${String(STALL_MS)}ms 后 setInterval(500ms) 首次触发`,
			first >= 500 - JIFFIE_SLACK_MS && first <= 500 + LATENESS_BUDGET_MS,
			`实际 ${String(first)}ms`
		);

		// 卡顿发生在定时器回调内部，回调里再武装一个：同样不能提前响
		const armedInside = await this.timed(() => new Promise<void>((resolve) => {
			setTimeout(() => {
				busy(STALL_MS);
				setTimeout(resolve, 500);
			}, 30);
		}));
		check(
			`回调内卡顿 ${String(STALL_MS)}ms 后再武装 setTimeout(500ms)`,
			armedInside >= 500 - JIFFIE_SLACK_MS && armedInside <= STALL_MS + 500 + LATENESS_BUDGET_MS,
			`实际 ${String(armedInside)}ms`
		);
	}

	private async checkSetImmediate(): Promise<void> {
		let fired = false;
		await new Promise<void>((resolve) => {
			setImmediate(() => {
				fired = true;
				resolve();
			});
		});
		check("setImmediate 能跑", fired, "没有触发");
	}

	private finish(error?: unknown): void {
		if (this.finished) {
			return;
		}
		this.finished = true;
		reportTestFailure("timer-clock", error);
		this.get_tree()?.quit();
	}
}
