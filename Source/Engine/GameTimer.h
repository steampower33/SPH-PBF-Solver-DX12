#pragma once

class GameTimer
{
public:
	GameTimer() : mSecondsPerCount(0.0), mDeltaTime(-1.0), mBaseTime(0), mPausedTime(0),
		mPrevTime(0), mCurrTime(0), mStopped(false), mTotalTime(0.0f)
	{
		__int64 countsPerSec;
		QueryPerformanceFrequency((LARGE_INTEGER*)&countsPerSec);
		mSecondsPerCount = 1.0 / (double)countsPerSec;
	}

	void Reset()
	{
		__int64 currTime;
		QueryPerformanceCounter((LARGE_INTEGER*)&currTime);
		mBaseTime = currTime;
		mPrevTime = currTime;
		mStopped = false;
		mTotalTime = 0.0f;
	}

	void Tick()
	{
		if (mStopped)
		{
			mDeltaTime = 0.0;
			return;
		}

		__int64 currTime;
		QueryPerformanceCounter((LARGE_INTEGER*)&currTime);
		mCurrTime = currTime;

		mDeltaTime = (mCurrTime - mPrevTime) * mSecondsPerCount;
		mPrevTime = mCurrTime;

		if (mDeltaTime < 0.0)
		{
			mDeltaTime = 0.0;
		}

		mTotalTime += (float)mDeltaTime;
	}

	float GetTotalTime() const
	{
		if (mStopped)
		{
			return (float)((mPausedTime - mBaseTime) * mSecondsPerCount);
		}
		else
		{
			return (float)((mCurrTime - mBaseTime - mPausedTime) * mSecondsPerCount);
		}
	}
	float GetDeltaTime() const { return (float)mDeltaTime; }

private:
	double mSecondsPerCount;
	double mDeltaTime;

	__int64 mBaseTime;
	__int64 mPausedTime;
	__int64 mPrevTime;
	__int64 mCurrTime;
	bool mStopped;
	float mTotalTime;
};