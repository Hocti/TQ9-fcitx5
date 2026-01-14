using System.Collections.Generic;
using System.Data.SQLite;
using System.Diagnostics;
using System.Globalization;
using System.Text;

namespace Q9CS
{

    class Q9Core
    {
        private SQLiteConnection con;

        public Q9Core()
        {
            string cs = "Data Source=files/dataset.db";
            con = new SQLiteConnection(cs);
            con.Open();
        }

        private string[] sql2strs(string stm, string splitStr = "")
        {
            SQLiteCommand cmd = new SQLiteCommand(stm, con);
            object result = cmd.ExecuteScalar();
            if (result != null)
            {
                string str = result.ToString();
                StringInfo stru8 = new System.Globalization.StringInfo(str);

                //char[] charArr = str.ToCharArray();

                if (splitStr == "")
                {
                    string[] strs = new string[stru8.LengthInTextElements];
                    for (int i = 0, t = stru8.LengthInTextElements; i < t; i++)
                    {
                        strs[i] = stru8.SubstringByTextElements(i, 1);
                    }
                    return strs;
                }
                else
                {
                    return str.
                }
            }
            else
            {
                //Debug.WriteLine($"{key} : no result");
                return null;
            }

        }

        public string[] keyInput(int key)
        {
            return sql2strs($"SELECT characters FROM `mapped_table` WHERE id='{key}'");
        }

        public string[] getRelate(string word)
        {
            return sql2strs($"SELECT candidates FROM `related_candidates_table` WHERE character='{word}'", " ");
        }

        public int[] getCode(string word) {
            string stm = $"SELECT `id` FROM `mapped_table` WHERE INSTR(`characters`,'{word}');";
            var nums = new List<int>();
            using (var command = new SQLiteCommand(stm, con))
            {
                using (var reader = command.ExecuteReader())
                {
                    while (reader.Read())
                    {
                        int num = reader.GetInt32(0);
                        nums.Add(num);
                    }
                }
            }
            return nums.ToArray();
        }

        public string[] getHomo(string word)
        {

            var words = new List<string>();
            if (word.Length > 1) return words.ToArray();

            string stm = $"SELECT w1.char FROM word_meta w1 INNER JOIN word_meta w2 ON w1.ping = w2.ping WHERE w2.char = '{word}' ORDER BY CASE WHEN w1.ping2 = w2.ping2 THEN 0 ELSE 1 END ASC;";//, w1.freq


            using (var command = new SQLiteCommand(stm, con))
            {
                // Use parameters to prevent SQL injection
               // command.Parameters.AddWithValue("@InputWord", inputWord);

                using (var reader = command.ExecuteReader())
                {
                    while (reader.Read())
                    {
                        string chr= reader["char"].ToString();
                        //if (chr == word) continue;
                        words.Add(chr);
                    }
                }
            }

            return words.ToArray();
        }


        public string tcsc(string input)
        {
            StringBuilder output = new StringBuilder();
            foreach (char c in input)
            {
                string stm = $"SELECT `simplified` FROM `ts_chinese_table` WHERE `traditional`='{c}' LIMIT 1";
                SQLiteCommand cmd = new SQLiteCommand(stm, con);
                object result = cmd.ExecuteScalar();
                if (result != null)
                {
                    string str = result.ToString();
                    output.Append(str);
                }
                else
                {
                    output.Append(c);
                }
            }
            return output.ToString();
        }

    }
}
