//手写策略的自动调参：用自对局逐个调整 HandwrittenParams 里的参数，留下让平均分显著变高的改动
//
//做法（坐标搜索 + 配对比较）：
//  每一轮换一批随机种子，先用当前参数跑一遍作为基准；
//  然后每个参数分别试 ×(1+r) 和 ×(1-r)，用同一批种子对局，逐局求分差；
//  分差的平均值超过 2 个标准误才接受；每轮结束后步长 r 缩小。
//  最后用一批新种子、更多局数比较默认参数和调好的参数。
//
//环境变量：
//  UMAAI_TUNE_CONFIGS  逗号分隔的配置文件，默认 ../ConfigTemplate/testConfig.json；多个卡组时按总分比较，避免只适合一套卡组
//  UMAAI_TUNE_GAMES    每个配置每次评估的局数，默认 8000
//  UMAAI_TUNE_ROUNDS   轮数，默认 6
//  UMAAI_TUNE_START    初始参数 json（可选），默认用代码里的默认值
//结果写到 ./handwrittenParams_tuned.json
#include <iostream>
#include <iomanip>
#include <sstream>
#include <thread>
#include <vector>
#include <string>
#include <cmath>
#include <cstdlib>
#include <random>
#include "../Game/Game.h"
#include "../NeuralNet/Evaluator.h"
#include "../GameDatabase/GameDatabase.h"
#include "TestConfig.h"
#include "tests.h"
using namespace std;

namespace
{
  vector<TestConfig> configs;
  int gamesPerConfig = 8000;

  int envInt(const char* name, int def)
  {
    const char* v = getenv(name);
    return v != nullptr ? atoi(v) : def;
  }

  //用参数P跑所有配置的对局，第g局的种子是seedBase+g，返回每局的分数（各配置依次排列）
  vector<double> evaluate(const HandwrittenParams& P, uint64_t seedBase)
  {
    int total = int(configs.size()) * gamesPerConfig;
    vector<double> scores(total);
    int threadNum = max(1u, thread::hardware_concurrency());
    vector<thread> threads;
    for (int t = 0; t < threadNum; t++)
    {
      threads.push_back(thread([&, t]() {
        for (int i = t; i < total; i += threadNum)
        {
          const TestConfig& c = configs[i / gamesPerConfig];
          mt19937_64 rand(seedBase + i);
          Game game;
          game.newGame(rand, GameSettings(), c.umaId, c.umaStars, (int*)&c.cards[0], (int*)&c.zhongmaBlue[0], (int*)&c.zhongmaBonus[0]);
          game.gameSettings.eventStrength = c.eventStrength;
          while (!game.isEnd())
            game.applyAction(rand, Evaluator::handWrittenStrategy(game, P));
          scores[i] = game.finalScore();
        }
        }));
    }
    for (auto& th : threads)th.join();
    return scores;
  }

  double mean(const vector<double>& v)
  {
    double s = 0;
    for (double x : v)s += x;
    return s / v.size();
  }

  //配对分差的平均值和标准误
  void pairedDiff(const vector<double>& a, const vector<double>& b, double& m, double& se)
  {
    int n = int(a.size());
    double s = 0, s2 = 0;
    for (int i = 0; i < n; i++)
    {
      double d = a[i] - b[i];
      s += d;
      s2 += d * d;
    }
    m = s / n;
    double var = s2 / n - m * m;
    se = sqrt(max(0.0, var) / n);
  }

  void printParams(const HandwrittenParams& P, const HandwrittenParams& def)
  {
    cout << defaultfloat << setprecision(6);
    for (int i = 0; i < HandwrittenParams::NUM; i++)
    {
      double v = P.*HandwrittenParams::members[i];
      double d = def.*HandwrittenParams::members[i];
      cout << "  " << left << setw(26) << HandwrittenParams::names[i] << right << setw(10) << v;
      if (fabs(v - d) > 1e-9)
        cout << "  （默认 " << d << "）";
      cout << endl;
    }
  }
}

void main_tuneHandwritten()
{
  GameDatabase::loadTranslation("../db/text_data.json");
  GameDatabase::loadUmas("../db/umaDB.json");
  GameDatabase::loadDBCards("../db/cardDB.json");

  const char* configEnv = getenv("UMAAI_TUNE_CONFIGS");
  string configList = configEnv != nullptr ? configEnv : "../ConfigTemplate/testConfig.json";
  {
    stringstream ss(configList);
    string path;
    while (getline(ss, path, ','))
      if (!path.empty())
        configs.push_back(TestConfig::loadFile(path));
  }
  gamesPerConfig = envInt("UMAAI_TUNE_GAMES", 8000);
  int rounds = envInt("UMAAI_TUNE_ROUNDS", 6);

  const HandwrittenParams def;
  HandwrittenParams cur;
  const char* startEnv = getenv("UMAAI_TUNE_START");
  if (startEnv != nullptr && !cur.loadJson(startEnv))
    cout << "读不到初始参数 " << startEnv << "，用默认值" << endl;

  cout << "卡组数 " << configs.size() << "，每次评估每个卡组 " << gamesPerConfig << " 局，共 " << rounds << " 轮" << endl;

  double r = 0.3;
  for (int round = 0; round < rounds; round++)
  {
    uint64_t seedBase = 1000000007ull * (round + 1);
    vector<double> curScores = evaluate(cur, seedBase);
    cout << "第 " << round + 1 << " 轮，步长 " << r << "，当前平均分 " << fixed << setprecision(1) << mean(curScores) << endl;
    int accepted = 0;
    for (int i = 0; i < HandwrittenParams::NUM; i++)
    {
      double x = cur.*HandwrittenParams::members[i];
      double tries[2];
      if (fabs(x) < 1e-9)//默认是0的参数用加法步长
      {
        tries[0] = 300 * r;
        tries[1] = -300 * r;
      }
      else
      {
        tries[0] = x * (1 + r);
        tries[1] = x * (1 - r);
      }
      for (double y : tries)
      {
        HandwrittenParams cand = cur;
        cand.*HandwrittenParams::members[i] = y;
        vector<double> s = evaluate(cand, seedBase);
        double m, se;
        pairedDiff(s, curScores, m, se);
        if (m > 0 && m > 2 * se)
        {
          cout << "  " << HandwrittenParams::names[i] << ": " << defaultfloat << setprecision(6) << x << " -> " << y
            << fixed << setprecision(1) << "，平均分 +" << m << "（±" << se << "）" << endl;
          cur = cand;
          curScores = s;
          accepted++;
          break;
        }
      }
    }
    cout << "  本轮接受 " << accepted << " 个改动" << endl;
    r *= 0.7;
  }

  cur.saveJson("handwrittenParams_tuned.json");
  cout << endl << "调参结果（已写入 handwrittenParams_tuned.json）：" << endl;
  printParams(cur, def);

  //用新种子、4倍局数验证
  gamesPerConfig *= 4;
  uint64_t seedBase = 987654321ull;
  vector<double> a = evaluate(cur, seedBase);
  vector<double> b = evaluate(def, seedBase);
  double m, se;
  pairedDiff(a, b, m, se);
  cout << endl << "验证（新种子，每个卡组 " << gamesPerConfig << " 局）：默认 " << mean(b) << "，调参后 " << mean(a)
    << "，差 " << m << "（±" << se << "）" << endl;
  for (int c = 0; c < int(configs.size()); c++)
  {
    double sa = 0, sb = 0;
    for (int g = 0; g < gamesPerConfig; g++)
    {
      sa += a[c * gamesPerConfig + g];
      sb += b[c * gamesPerConfig + g];
    }
    cout << "  卡组 " << c + 1 << "：默认 " << sb / gamesPerConfig << "，调参后 " << sa / gamesPerConfig << endl;
  }
}
