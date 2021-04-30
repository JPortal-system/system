#include <atomic>
#include <fstream>
#include <iostream>
#include <list>
#include <map>
#include <pthread.h>
#include <set>
#include <stdlib.h>
#include <string>
#include <unistd.h>
#include <vector>
using namespace std;
const int maxThread = 16;
atomic_int curThread{0};

const int minLossSize = 100000;
const int expectedLossSize = 300000;
const int maxLossSize = 500000;
const int ultLossSize = 10 * expectedLossSize;
const int anchorLen = 100;
const int anchorBL = 5 * anchorLen;
int compareSuffix(int suffix[], list<vector<int>>::iterator iter, int k,
                  int &step) {
  int traceSize = iter->size();
  int score = 0;
  int i = 0;
  for (; i < anchorLen && k < traceSize; ++i, ++k) {
    ++step;
    if (suffix[i] != (*iter)[k]) {
      return score;
    }
    ++score;
    if (suffix[i] > 255)
      score += 4;
  }
  return score;
}

int comparePrefix(int prefix[], list<vector<int>>::iterator iter, int k,
                  int &step) {
  int i = anchorLen - 1;
  int score = 0;
  for (; i >= 0 && k >= 0; --i, --k) {
    ++step;
    if (prefix[i] != (*iter)[k]) {
      return score;
    }
    ++score;
    if (prefix[i] > 255)
      score += 4;
  }
  return score;
}

struct ThArg {
  int *prefix;
  int *suffix;
  list<vector<int>>::iterator trace;
  pair<int, int> *site;
  int *score;
  ThArg(int *_prefix, int *_suffix, list<vector<int>>::iterator _trace,
        pair<int, int> *_site, int *_score) {
    this->prefix = _prefix;
    this->suffix = _suffix;
    this->trace = _trace;
    this->site = _site;
    this->score = _score;
  }
};

void *selectOne(void *thArg) {
  pthread_detach(pthread_self());
  struct ThArg *ta = (struct ThArg *)thArg;
  int *prefix = ta->prefix;
  int *suffix = ta->suffix;
  list<vector<int>>::iterator trace = ta->trace;
  pair<int, int> *site = ta->site;
  int *score = ta->score;
  delete (ta);
  //
  int traceSize = trace->size();
  list<pair<int, int>> prefix_score;
  list<pair<int, int>> suffix_score;
  int end = traceSize - minLossSize;
  for (int i = 0; i < end; ++i) {
    int step = -1;
    int score = comparePrefix(prefix, trace, i, step);
    if (score > 5) {
      prefix_score.push_back(make_pair(i, score));
      i += step;
    }
  }
  for (int i = minLossSize; i < traceSize; ++i) {
    int step = -1;
    int score = compareSuffix(suffix, trace, i, step);
    if (score > 5) {
      suffix_score.push_back(make_pair(i, score));
      i += step;
    }
  }
  // --curCmp;
  if (prefix_score.size() == 0 && suffix_score.size() == 0) {
    site->first = 0;
    site->second = 0;
    *score = 0;
  } else if (prefix_score.size() == 0) {
    if (suffix_score.back().first < expectedLossSize) {
      // sites.push_back(make_pair(0, suffix_score.back().first));
      // site_scores.push_back(suffix_score.back().second);
      site->first = 0;
      site->second = suffix_score.back().first;
      *score = suffix_score.back().second;
    } else {
      int max = 0;
      int index = 0;
      for (auto ss : suffix_score) {
        if (ss.first >= expectedLossSize) {
          if (max < ss.second) {
            max = ss.second;
            index = ss.first;
          }
        }
      }
      // sites.push_back(make_pair(index - expectedLossSize, index));
      // site_scores.push_back(max);
      site->first = index - expectedLossSize;
      site->second = index;
      *score = max;
    }

  } else if (suffix_score.size() == 0) {
    if (prefix_score.front().first + expectedLossSize > traceSize) {
      // sites.push_back(make_pair(prefix_score.front().first + 1,
      // traceSize)); site_scores.push_back(prefix_score.front().second);
      site->first = prefix_score.front().first + 1;
      site->second = traceSize;
      *score = prefix_score.front().second;
    } else {
      int max = 0;
      int index = 0;
      for (auto ss : prefix_score) {
        if (ss.first + expectedLossSize <= traceSize) {
          if (max < ss.second) {
            max = ss.second;
            index = ss.first;
          }
        } else {
          break;
        }
      }
      // sites.push_back(make_pair(index + 1, index + expectedLossSize));
      // site_scores.push_back(max);
      site->first = index + 1;
      site->second = index + expectedLossSize;
      *score = max;
    }
  } else {
    int prefix_max = 0;
    int suffix_max = 0;
    int prefix_index = 0;
    int suffix_index = 0;
    list<pair<int, int>>::iterator suffix_score_iter = suffix_score.begin();
    for (auto prefix_ss : prefix_score) {
      if (prefix_ss.first + minLossSize >= suffix_score.back().first)
        break;
      bool isSet = false;
      for (auto suffix_ss = suffix_score_iter; suffix_ss != suffix_score.end();
           ++suffix_ss) {
        if (prefix_ss.first + maxLossSize <= suffix_ss->first)
          break;
        if (prefix_ss.first + minLossSize < suffix_ss->first) {
          if (!isSet) {
            suffix_score_iter = suffix_ss;
            isSet = true;
          }
          if (prefix_ss.second + suffix_ss->second > prefix_max + suffix_max) {
            suffix_score_iter = suffix_ss;
            prefix_max = prefix_ss.second;
            suffix_max = suffix_ss->second;
            prefix_index = prefix_ss.first;
            suffix_index = suffix_ss->first;
          } else if (prefix_ss.second + suffix_ss->second ==
                         prefix_max + suffix_max &&
                     suffix_ss->first - prefix_ss.first >
                         suffix_index - prefix_index) {
            suffix_score_iter = suffix_ss;
            prefix_max = prefix_ss.second;
            suffix_max = suffix_ss->second;
            prefix_index = prefix_ss.first;
            suffix_index = suffix_ss->first;
          }
        }
      }
    }
    if (prefix_index == suffix_index) {
      suffix_score_iter = suffix_score.begin();
      for (auto prefix_ss : prefix_score) {
        if (prefix_ss.first + maxLossSize > suffix_score.back().first)
          break;
        bool isSet = false;
        for (auto suffix_ss = suffix_score_iter;
             suffix_ss != suffix_score.end(); ++suffix_ss) {
          // if (prefix_ss.first + ultLossSize <= suffix_ss->first)
          //   break;
          if (prefix_ss.first + maxLossSize <= suffix_ss->first) {
            if (!isSet) {
              suffix_score_iter = suffix_ss;
              isSet = true;
            }
            if (prefix_ss.second + suffix_ss->second >
                prefix_max + suffix_max) {
              suffix_score_iter = suffix_ss;
              prefix_max = prefix_ss.second;
              suffix_max = suffix_ss->second;
              prefix_index = prefix_ss.first;
              suffix_index = suffix_ss->first;
            } else if (prefix_ss.second + suffix_ss->second ==
                           prefix_max + suffix_max &&
                       suffix_ss->first - prefix_ss.first <
                           suffix_index - prefix_index) {
              suffix_score_iter = suffix_ss;
              prefix_max = prefix_ss.second;
              suffix_max = suffix_ss->second;
              prefix_index = prefix_ss.first;
              suffix_index = suffix_ss->first;
            }
          }
        }
      }
    }
    if (prefix_index == suffix_index) {
      prefix_index = prefix_score.front().first;
      suffix_index = suffix_score.back().first;
      prefix_max = prefix_score.front().second;
      suffix_max = suffix_score.back().second;
    }
    if (suffix_index - prefix_index >= ultLossSize) {
      if (suffix_max > prefix_max) {
        prefix_index = suffix_index - expectedLossSize;
        prefix_max = -anchorBL;
      } else {
        suffix_index = prefix_index + expectedLossSize;
        suffix_max = -anchorBL;
      }
    }
    // sites.push_back(make_pair(prefix_index + 1, suffix_index));
    // site_scores.push_back(prefix_max + suffix_max + anchorBL);
    site->first = prefix_index + 1;
    site->second = suffix_index;
    *score = prefix_max + suffix_max + anchorBL;
  }
  --curThread;
  pthread_exit(NULL);
}

int recover(int prefix[], int suffix[], list<vector<int>> &traces,
            ofstream &out) {
  int traceCount = traces.size();
  vector<pair<int, int>> sites(traceCount); // [ , )
  vector<int> site_scores(traceCount);
  for (int i = 0; i < traceCount; ++i) {
    site_scores[i] = -1;
    sites[i].first = -1;
    sites[i].second = -1;
  }
  vector<pthread_t> tids(traceCount);
  auto iter = traces.begin();
  int tid_i = 0;
  curThread = 0;
  // Select one for each trace
  while (iter != traces.end()) {
    //参数依次是：创建的线程id，线程参数，调用的函数，传入的函数参数
    struct ThArg *ta =
        new ThArg(prefix, suffix, iter, &sites[tid_i], &site_scores[tid_i]);
    int ret = pthread_create(&tids[tid_i], NULL, selectOne, (void *)ta);
    if (ret != 0) {
      cout << "pthread_create error: error_code=" << ret << endl;
      delete (ta);
    } else {
      // ++curCmp;
      ++curThread;
      ++tid_i;
      ++iter;
    }
    while (curThread >= maxThread) {
      // int completeCount = 0;
      // for (auto ss : site_scores) {
      //   if (ss >= 0)
      //     ++completeCount;
      // }
      // cout << "[" << curCmp << "/" << curThread << "][" << completeCount <<
      // "/"
      //      << traceCount << "]         \r" << flush;
      // cout << "[" << curCmp << "/" << curThread << "][" << completeCount <<
      // "/"
      //      << traceCount << "]...\r" << flush;
    }
  }

  while (true) {
    int completeCount = 0;
    for (auto ss : site_scores) {
      if (ss >= 0)
        ++completeCount;
    }
    // cout << "[" << curCmp << "/" << curThread << "][" << completeCount << "/"
    //      << traceCount << "]         \r" << flush;
    // cout << "[" << curCmp << "/" << curThread << "][" << completeCount << "/"
    //      << traceCount << "]...\r" << flush;
    if (completeCount >= traceCount)
      break;
    // sleep(1);
  }

  if (sites.empty()) {
    out << "#loss" << endl;
    cout << "size: 0" << endl;
    return 0;
  }

  int index = 0;
  int begin = 0, end = 0;
  int iter_index = 0;
  int iter_score = 0;
  // Select one with both-ends matched from all traces
  auto site_score_iter = site_scores.begin();
  for (int i = 0; i < traceCount; ++i) {
    auto site = sites[i];
    int size = site.second - site.first;
    if (size > 0) {
      if (*site_score_iter > anchorBL && iter_score < *site_score_iter) {
        begin = site.first;
        end = site.second;
        iter_index = index;
        iter_score = *site_score_iter;
      }
    }
    ++index;
    ++site_score_iter;
  }
  // If not, select one with one-end matched from all traces
  if (end == begin) {
    site_score_iter = site_scores.begin();
    for (int i = 0; i < traceCount; ++i) {
      auto site = sites[i];
      int size = site.second - site.first;
      if (size > 0) {
        if (iter_score < *site_score_iter) {
          begin = site.first;
          end = site.second;
          iter_index = index;
          iter_score = *site_score_iter;
        }
      }
      ++index;
      ++site_score_iter;
    }
  }
  // output
  index = 0;
  for (auto trace : traces) {
    if (index == iter_index) {
      for (int i = begin; i < end; ++i) {
        out << trace[i] << endl;
      }
      break;
    }
    ++index;
  }
  out << "#loss" << endl;
  cout << "size: " << end - begin << "\t| " << iter_score << endl;
  return 0;
}

int main(int argc, char **argv) {
  if (argc < 2) {
    cout << "Input file missing" << endl;
    exit(1);
  }
  ifstream in(argv[1]);
  if (!in.is_open()) {
    cout << "Error opening input file";
    exit(1);
  }
  ofstream out(argv[2]);
  if (!out.is_open()) {
    cout << "Error opening output file";
    exit(1);
  }
  cout << "Loading traces..." << endl;
  list<vector<int>> traces;

  char buffer[256];
  int traceLen = 0;
  while (!in.eof()) {
    in.getline(buffer, 256);
    if (buffer[0] == '\0')
      continue;
    if (buffer[0] == '#') {
      vector<int> trace(traceLen);
      traces.push_back(trace);
      traceLen = 0;
    } else {
      ++traceLen;
    }
  }
  {
    vector<int> trace(traceLen);
    traces.push_back(trace);
    traceLen = 0;
  }

  in.clear();
  in.seekg(ios::beg);
  auto iter = traces.begin();
  while (!in.eof()) {
    in.getline(buffer, 256);
    if (buffer[0] == '\0')
      continue;
    if (buffer[0] == '#') {
      traceLen = 0;
      ++iter;
    } else {
      (*iter)[traceLen] = atoi(buffer);
      ++traceLen;
    }
  }
  in.close();
  cout << "Loading completed." << endl;
  cout << "Beginning recovery" << endl;

  int lossCount = traces.size() - 1;
  iter = traces.begin();
  for (int i = 0; i < lossCount; ++i) {
    cout << "loss" << i << endl;
    int prefix[anchorLen]{0};
    int suffix[anchorLen]{0};
    for (int j = anchorLen - 1, k = iter->size() - 1; j >= 0 && k >= 0;
         --j, --k) {
      prefix[j] = (*iter)[k];
    }
    ++iter;
    for (int j = 0; j < anchorLen && j < iter->size(); ++j) {
      suffix[j] = (*iter)[j];
    }
    recover(prefix, suffix, traces, out);
  }
  return 0;
}