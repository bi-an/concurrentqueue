import pandas as pd
import plotly.express as px
import glob
import os

# 查找当前目录下所有 CSV 文件
csv_files = glob.glob("*.csv")
if not csv_files:
    raise FileNotFoundError("当前目录没有 CSV 文件！")

for csv_file in csv_files:
    print(f"处理 CSV 文件: {csv_file}")

    # 读取 CSV
    df = pd.read_csv(csv_file, skipinitialspace=True)
    df.columns = [c.strip().rstrip(',') for c in df.columns]  # 清洗列名

    # 将 threads 转成字符串便于横坐标显示
    if 'threads' not in df.columns:
        print(f"{csv_file} 没有 threads 列，跳过")
        continue
    df['threads'] = df['threads'].astype(str)

    # 自动选取除 threads 外的所有列作为 y 轴
    y_cols = [col for col in df.columns if col != 'threads']
    if not y_cols:
        print(f"{csv_file} 没有有效数据列，跳过")
        continue

    # 绘制折线图
    fig = px.line(
        df,
        x='threads',
        y=y_cols,
        markers=True,
        title=f'Queue Benchmark: {csv_file}'
    )

    fig.update_layout(
        xaxis_title='Number of Threads',
        yaxis_title='Throughput (ops/sec)',
        yaxis_type='log',  # 对数坐标，更好显示差异
        legend_title='Queue Type'
    )

    # 生成 HTML 文件名
    html_file = os.path.splitext(csv_file)[0] + "_benchmark.html"
    fig.write_html(html_file)
    print(f"生成 HTML 文件: {html_file}")

print("所有 CSV 文件处理完成！")



# import pandas as pd
# import plotly.express as px

# # 读取 CSV
# df = pd.read_csv('heavy.csv')

# # 将线程数转成字符串便于横坐标显示
# df['threads'] = df['threads'].astype(str)

# # 绘制吞吐量对比图
# fig = px.line(
#     df,
#     x='threads',
#     y=[
#         'std::queue + std::mutex',
#         'boost::lockfree::queue',
#         'tbb::concurrent_queue',
#         'moodycamel::ConcurrentQueue (no tokens)',
#         'moodycamel::ConcurrentQueue',
#         'moodycamel::ConcurrentQueue (bulk)'
#     ],
#     markers=True,
#     title='Queue Benchmark: Throughput vs Threads'
# )

# fig.update_layout(
#     xaxis_title='Number of Threads',
#     yaxis_title='Throughput (ops/sec)',
#     yaxis_type='log',  # 对数坐标，更好展示差距
#     legend_title='Queue Type'
# )

# fig.show()  # 在浏览器显示交互图
# fig.write_html('heavy_benchmark.html')  # 保存 HTML
