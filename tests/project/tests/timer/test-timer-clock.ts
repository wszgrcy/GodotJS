import { Node } from "godot";
import { reportTestCompleted, reportTestFailure } from "../test-status";

/**
 * 定时器时钟用例：引擎每帧给 JS 定时器轮喂一次「这一帧过了多久」，定时器轮只把收到的
 * 毫秒数累加，自己不看表。喂少了定时器就跟着变慢，而且不报错，所以只能从外面量。
 *
 * 守住的是「首帧猛推」：第一帧喂的是「本帧时间戳减 0」，等于把引擎从启动到第一帧的总时长
 * 一次性灌进定时器轮，于是启动阶段注册的 `setTimeout(fn, 50)` 会在第一帧立刻触发
 * （墙上时间才 0ms）。
 *
 * 为什么单独起进程、把本场景当主场景跑，而不是挂进 `start.ts` 的场景清单：首帧猛推只在
 * 进程的第一帧看得见，等 `start.ts` 把场景加载进来时早就过去了。跑法见
 * `run-runtime-matrix.mts` 里标了 `timer clock` 的那一步。
 *
 * 定时器停摆时 `await` 根本不返回，所以整体挂一个墙上时间兜底，到点直接判负退出。
 */

/** 整个用例的时间预算，超了就判负。 */
const BUDGET_MS = 5000;

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

export default class TimerClock extends Node {
	private startedAtMs = Date.now();
	private finished = false;

	_ready(): void {
		setTimeout(() => {
			this.finish("超过 5000ms 没跑完，定时器没有触发");
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
