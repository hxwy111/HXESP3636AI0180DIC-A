from pathlib import Path
from xml.sax.saxutils import escape
from zipfile import ZipFile, ZIP_DEFLATED
import hashlib
import json
import re
import shutil
from PIL import Image
from pypdf import PdfReader

WORK = Path(__file__).resolve().parent
ROOT = WORK.parent.parent.parent
SOURCE = next(p for p in ROOT.iterdir() if p.is_dir() and p.name.startswith("TP"))
OUT = WORK / "report"
OUT.mkdir(exist_ok=True)
NAME = "CST816D_CST810_CST820_触摸芯片替换评估报告"
DATE = "2026-10-30"
W = "http://schemas.openxmlformats.org/wordprocessingml/2006/main"
R = "http://schemas.openxmlformats.org/officeDocument/2006/relationships"
sources = [
    ("S1", "CST816D数据手册V1.0.pdf", "第1、2、4、6、7、10、11、12页"),
    ("S2", "CST810.pdf", "第1、2、3、4、6、8、9、10、11页"),
    ("S3", "CST820数据手册V1.1.pdf", "第1、2、3、4、6、7、10、11、12页"),
    ("S4", "HXESP3636AI0180DIC-A.png", "U12/CTP、H2、GPIO分配表、I²C上拉电路"),
]
for _, filename, _ in sources:
    assert (SOURCE / filename).is_file(), filename
for filename, expected in [("CST810.pdf", "10"), ("CST816D数据手册V1.0.pdf", "13"), ("CST820数据手册V1.1.pdf", "17")]:
    first = PdfReader(SOURCE / filename).pages[0].extract_text()
    assert expected in first and "2.8V" in first, filename

specs = [
    ["项目", "CST816D（现有基准）", "CST810", "CST820"],
    ["外围引脚/本体", "QFN20 / 3×3 mm", "QFN20 / 3×3 mm", "QFN24 / 3×3 mm"],
    ["引脚间距 / 典型厚度", "0.40 / 0.55 mm", "0.40 / 0.55 mm", "0.35 / 0.55 mm"],
    ["最大感应通道", "13路", "10路", "17路"],
    ["供电 / 接口电平", "2.8～3.6 V / 支持3.3 V", "2.8～3.6 V / 支持3.3 V", "2.8～3.6 V / 支持3.3 V"],
    ["I²C频率 / 常用7位地址", "10～400 kHz / 0x15", "10～400 kHz / 0x15", "10～400 kHz / 0x15"],
    ["CMOD电容", "1～5.6 nF，通常1 nF", "1～5.6 nF，通常1 nF", "1～5.6 nF，通常1 nF"],
    ["建议TP尺寸", "1.8英寸以内", "1.8英寸以内", "2英寸以内"],
    ["现有板直接换芯", "原设计器件", "不支持：感应脚及通道数不同", "不支持：封装及脚位不同"],
    ["改板评估结论", "保留现有方案", "不推荐用于现有12路接线方案", "可继续评估，需改板和厂家调参"],
]
host = [
    ["信号", "现有连接", "CST816D脚号", "CST810脚号", "CST820脚号"],
    ["VDDA", "3.3 V", "16", "16", "19"],
    ["RST", "GPIO40", "17", "17", "21"],
    ["SCL", "GPIO14", "18", "18", "22"],
    ["SDA", "GPIO15", "19", "19", "23"],
    ["IRQ / INT", "GPIO11，R16上拉10 kΩ", "20", "20", "24"],
    ["CMOD0 / CMOD1", "C27 / C28：各1 nF到地", "1 / 15", "1 / 15", "1 / 18"],
]
p816 = ["CMOD0", "S1L", "S2L", "S3L", "S4L", "S5L", "S6L", "S7R", "S8R", "S9R", "S10R", "S11R", "S12R", "S13R", "CMOD1", "VDDA", "RST", "SCL", "SDA", "IRQ"]
p810 = ["CMOD0", "ID1", "S1L", "S2L", "S3L", "S4L", "S5L", "NC", "S6R", "S7R", "S8R", "S9R", "S10R", "ID2", "CMOD1", "VDDA", "RST", "SCL", "SDA", "IRQ"]
p820 = ["CMOD0"] + [f"S{i:02}" for i in range(1, 17)] + ["CMOD1", "VDDA", "S17", "RST", "SCL", "SDA", "INT"]
nets = ["C27，1 nF到地"] + [f"CTP_S{i}" for i in range(1, 13)] + ["未连接", "C28，1 nF到地", "3V3", "TP_RST", "TP_SCL", "TP_SDA", "TP_INT"]
pins = [["脚号", "CST816D", "CST810", "CST820", "本板CST816D现有网络"]]
for i in range(24):
    pins.append([str(i + 1), p816[i] if i < 20 else "—", p810[i] if i < 20 else "—", p820[i], nets[i] if i < 20 else "—"])
pins.append(["底部焊盘", "GND（原理图标21）", "GND", "GND", "GND"])

verification = [
    ["阶段", "核查项目", "通过条件 / 输出"],
    ["设计输入", "TP电极图、通道顺序、PCB焊盘、厂家固件与寄存器表", "获得明确的通道映射和可追溯固件版本"],
    ["原理图/PCB", "封装、底焊盘、供电、CMOD、感应走线、I²C/INT/RST", "完成逐网核对；去耦布局及电容材质符合手册"],
    ["上电/通信", "3.3 V及纹波、复位时序、I²C地址、SDA/SCL上升沿", "供电2.8～3.6 V，纹波≤50 mV；通信稳定"],
    ["坐标/触点", "360×360全区域、四边/四角/中心、连续水平及垂直轨迹", "无明显死区、跳点、坐标异常；按下与释放完整"],
    ["连续操作", "多次滑动、长按、抬手重按，记录原始报文和INT", "达到项目约定测试时长/次数；无卡住或触点滞留"],
    ["模式/干扰", "待机/休眠唤醒、LCD动画、背光PWM、充电、无线活动", "各工况稳定；功耗、响应时间满足项目目标"],
]

sections = []
def section(title):
    entry = {"title": title, "items": []}
    sections.append(entry)
    return entry["items"]
def para(items, text, bold=False):
    items.append(("p", text, bold))
def sub(items, title):
    items.append(("h", title))
def table(items, rows, small=False):
    items.append(("table", rows, small))

s = section("1 评估结论与规格对照")
para(s, "项目：HXESP3636AI0180DIC-A｜1.8英寸、360×360显示与触摸系统")
para(s, f"版本：V1.0｜日期：{DATE}｜性质：基于所提供手册与原理图的设计评估，未做换芯实测")
para(s, "结论：CST810、CST820均不能在现有PCB上直接替换CST816D。CST810存在感应脚不兼容和最大通道数不足的问题；CST820通道数量充足，可作为改板后验证的候选方案。", True)
table(s, specs)
para(s, "判断边界：原理图显示12路CTP电极网络，未提供屏体电极图，不能确认这些电极的实际用途或断言任意通道重接后即可正常工作。")
para(s, "表内尺寸与参数为所给版本手册数据；相同3×3 mm外形不代表焊盘、引脚或固件兼容。两款候选芯片均不能作为免改板、免调参替代料。")

s = section("2 现有电路与可复用条件")
para(s, "依据[S4]：触摸控制器为板载U12（CST816D）；感应输入引出CTP_S1～CTP_S12，S13R（14脚）未连接。")
s.append(("image", WORK / "board_ctp.png", "图1 现有U12/CTP电路局部，裁自提供的原理图", 7.8))
table(s, host, True)
para(s, "主控侧：SDA/SCL与IMU、RTC共享总线；图中R13、R14各为10 kΩ上拉。改用CST820时可保留GPIO功能分配，但须将线路接到其新脚位，并复核总线电容、并联上拉及实际速率。")
para(s, "电源与电容：3.3 V在三款芯片的供电范围内；C27、C28各1 nF符合CMOD推荐容量。电容材质应核对为C0G/NP0、精度±10%或更好。U12附近仅画C26=100 nF，应核对并补齐贴近芯片的储能去耦；手册引脚说明推荐2.2～10 µF，应用规范示例为0.1 µF并10 µF。")
para(s, "接口边界：H2为原始电极接口，3～14脚对应S1～S12，1/15脚接地，2脚未接。它不是带供电及I²C的完整触摸模组接口，不能直接接入成品I²C触摸模组。")

s = section("3 逐脚功能对照")
para(s, "依据：CST816D[S1]第2/4页、CST810[S2]第2/3页、CST820[S3]第2/4页。表中比较的是脚号功能；不同封装中的相同脚号不代表物理焊盘位置一致。")
table(s, pins, True)
para(s, "CST810关键差异：2脚由感应输入变为ID1，8脚变为NC，14脚变为ID2，其余多路感应输入的顺序改变。原有CTP_S1及CTP_S7不能按现接线继续作为原感应输入使用。")
para(s, "CST820关键差异：外围引脚由20变为24、间距由0.40变为0.35 mm；CMOD1、VDDA、RST、SCL、SDA、INT均须按新封装重新布线。")

s = section("4 替代方案与实施要求")
sub(s, "4.1 保留CST816D：现有设计基准")
para(s, "现有原理图按CST816D设计，封装、主控连接及12路电极网络与其能力相符。若目标是最少硬件改动，应优先保留该芯片，并另行定位现有触摸异常；本报告不将过往滑动故障归因为芯片型号，也不证明现有样机性能合格。")
sub(s, "4.2 CST810：不推荐用于现有12路接线方案")
para(s, "主控接口、供电、CMOD和封装主要尺寸基本接近，但不是引脚兼容器件。最多10路感应能力无法一一承接图上12路电极网络；驱动软件或坐标方向设置不能增加物理感应通道。")
para(s, "如必须采用CST810，应先取得TP电极图，并由屏厂确认是否能使用10路或重新设计屏体电极方案，再修改PCB/FPC映射、处理ID1/ID2与NC、核对焊盘尺寸并取得专用固件。不能自行将两路电极短接、删除或随意合并。仅改PCB接线仍不能保证原屏体可用。")
sub(s, "4.3 CST820：可作为改板验证候选")
para(s, "最多17路感应输入，从数量上覆盖现有12路。供电、电平、I²C能力、低有效复位及可配置中断边沿具备与本主控系统配合的条件；没有CST810的通道数量短缺限制。")
para(s, "必须将U12改为QFN24、3×3 mm、0.35 mm间距封装，重布电源、控制线、CMOD和底部接地焊盘。也可评估专用转接板做样机验证，但需控制CMOD及感应走线长度，不能等同于原位换芯。")
para(s, "CTP_S1～CTP_S12映射至S01～S12可作为提交厂家核对的候选：对应CST820第2～13脚。此映射不是已验证方案；最终通道顺序、剩余S13～S17的处理及扫描配置由电极图和厂家固件确定。")
para(s, "取得适配现有玻璃、盖板厚度、贴合方式与LCD干扰环境的CST820固件及参数，重新验证边缘、轨迹和释放事件。供电去耦和CMOD容量虽可参考原值，仍需按实际本体电容与布局确认。")
sub(s, "4.4 三款芯片的软件兼容边界")
para(s, "三份手册均说明常用7位地址为0x15，但也明确项目地址可能不同。相同地址和I²C读写示例只证明通信机制相近，不能证明坐标、触点数、手势、芯片ID、休眠控制寄存器及启动配置兼容。应取得各型号项目固件的完整寄存器说明后适配。")
para(s, "三款上电/复位初始化时间均给出至少100 ms，复位低脉冲至少0.1 ms；具体驱动还应遵循厂家项目固件的时序要求。")

s = section("5 验证计划、资料依据与建议")
table(s, verification, True)
para(s, "测试说明：现有随机滑动失效现象可作为回归测试场景，但本报告没有运行新样机，也未形成故障根因结论。量产前应另外制定环境、ESD及可靠性验证标准。")
sub(s, "5.1 资料清单")
for code, filename, pages in sources:
    para(s, f"[{code}] {filename}；引用范围：{pages}。")
para(s, "资料位置：" + str(SOURCE))
sub(s, "5.2 尚待提供/确认的信息")
para(s, "① 触摸玻璃电极图、FPC定义和本体电容；② 现有PCB封装与布线文件、实际BOM；③ 候选芯片完整订货料号（CST820手册订货表为CST820-R）；④ 厂家项目固件、寄存器表及烧录方式；⑤ 实测电源、总线波形与替换样机结果。")
sub(s, "5.3 选型建议")
para(s, "不改PCB：继续使用CST816D。允许改PCB且希望保留现有屏体：优先推进CST820的厂家匹配和样机验证。CST810仅在厂家确认可采用10路电极方案或重新设计屏体时继续评估。", True)
para(s, "结论状态：资料评估完成；直接替换均不成立；CST820改板替代属于有条件可行，尚未达到样机验证或量产批准状态。")

def md_table(rows):
    def clean(s):
        return str(s).replace("|", "\\|").replace("\n", "<br>")
    return "\n".join(["| " + " | ".join(map(clean, rows[0])) + " |",
                      "| " + " | ".join(["---"] * len(rows[0])) + " |"] +
                     ["| " + " | ".join(map(clean, row)) + " |" for row in rows[1:]])

md = [f"# CST816D / CST810 / CST820 触摸芯片替换评估报告\n"]
for sec in sections:
    md.append("## " + sec["title"] + "\n")
    for item in sec["items"]:
        if item[0] == "p":
            md.append(("**" + item[1] + "**" if item[2] else item[1]) + "\n")
        elif item[0] == "h":
            md.append("### " + item[1] + "\n")
        elif item[0] == "table":
            md.append(md_table(item[1]) + "\n")
        elif item[0] == "image":
            image_name = NAME + "_原理图局部.png"
            shutil.copyfile(item[1], OUT / image_name)
            md.append(f"![{item[2]}]({image_name})\n")
(OUT / (NAME + ".md")).write_text("\n".join(md), encoding="utf-8-sig")

def text_run(text, bold=False, size=None, color=None):
    properties = ("<w:b/>" if bold else "")
    if size:
        properties += f'<w:sz w:val="{size}"/><w:szCs w:val="{size}"/>'
    if color:
        properties += f'<w:color w:val="{color}"/>'
    return f'<w:r><w:rPr>{properties}</w:rPr><w:t xml:space="preserve">{escape(str(text))}</w:t></w:r>'

def paragraph(text, style="Normal", bold=False, align=None, size=None):
    ppr = f'<w:pStyle w:val="{style}"/>'
    if align:
        ppr += f'<w:jc w:val="{align}"/>'
    return f'<w:p><w:pPr>{ppr}</w:pPr>{text_run(text, bold, size)}</w:p>'

def word_table(rows, compact=False):
    cols = len(rows[0])
    width = 10000 // cols
    xml = ['<w:tbl><w:tblPr><w:tblW w:w="10000" w:type="dxa"/><w:tblLayout w:type="fixed"/>',
           '<w:tblBorders>' + ''.join(f'<w:{x} w:val="single" w:sz="4" w:color="CBD5E1"/>' for x in ["top","left","bottom","right","insideH","insideV"]) + '</w:tblBorders>',
           '<w:tblCellMar><w:top w:w="45" w:type="dxa"/><w:bottom w:w="45" w:type="dxa"/><w:left w:w="65" w:type="dxa"/><w:right w:w="65" w:type="dxa"/></w:tblCellMar></w:tblPr>',
           '<w:tblGrid>' + ''.join(f'<w:gridCol w:w="{width}"/>' for _ in range(cols)) + '</w:tblGrid>']
    for i, row in enumerate(rows):
        xml.append('<w:tr><w:trPr><w:cantSplit/>' + ('<w:tblHeader/>' if i == 0 else '') + '</w:trPr>')
        for cell in row:
            fill = "E2EAF5" if i == 0 else ("F6F8FB" if i % 2 == 0 else "FFFFFF")
            xml.append(f'<w:tc><w:tcPr><w:tcW w:w="{width}" w:type="dxa"/><w:shd w:fill="{fill}"/><w:vAlign w:val="center"/></w:tcPr>')
            xml.append('<w:p><w:pPr><w:spacing w:before="0" w:after="0" w:line="240" w:lineRule="auto"/></w:pPr>' + text_run(cell, i == 0, 17 if compact else 18) + '</w:p></w:tc>')
        xml.append('</w:tr>')
    xml.append('</w:tbl>' + paragraph("", size=4))
    return ''.join(xml)

def word_image(path, caption, height_cm):
    with Image.open(path) as im:
        cx, cy = int(height_cm / 2.54 * 914400 * im.width / im.height), int(height_cm / 2.54 * 914400)
    drawing = f'''<w:p><w:pPr><w:jc w:val="center"/></w:pPr><w:r><w:drawing>
<wp:inline xmlns:wp="http://schemas.openxmlformats.org/drawingml/2006/wordprocessingDrawing">
<wp:extent cx="{cx}" cy="{cy}"/><wp:docPr id="1" name="现有CTP原理图" descr="U12 CST816D原理图局部"/>
<a:graphic xmlns:a="http://schemas.openxmlformats.org/drawingml/2006/main"><a:graphicData uri="http://schemas.openxmlformats.org/drawingml/2006/picture">
<pic:pic xmlns:pic="http://schemas.openxmlformats.org/drawingml/2006/picture"><pic:nvPicPr><pic:cNvPr id="1" name="board_ctp.png"/><pic:cNvPicPr/></pic:nvPicPr>
<pic:blipFill><a:blip r:embed="rIdImage"/><a:stretch><a:fillRect/></a:stretch></pic:blipFill>
<pic:spPr><a:xfrm><a:off x="0" y="0"/><a:ext cx="{cx}" cy="{cy}"/></a:xfrm><a:prstGeom prst="rect"><a:avLst/></a:prstGeom></pic:spPr></pic:pic>
</a:graphicData></a:graphic></wp:inline></w:drawing></w:r></w:p>'''
    return drawing + paragraph(caption, "Caption", align="center")

body = []
for i, sec in enumerate(sections):
    if i:
        body.append('<w:p><w:r><w:br w:type="page"/></w:r></w:p>')
    if i == 0:
        body.append(paragraph("触摸芯片替换评估报告", "Title"))
        body.append(paragraph("CST816D / CST810 / CST820", "Subtitle"))
    body.append(paragraph(sec["title"], "Heading1"))
    for item in sec["items"]:
        if item[0] == "p":
            body.append(paragraph(item[1], bold=item[2]))
        elif item[0] == "h":
            body.append(paragraph(item[1], "Heading2"))
        elif item[0] == "table":
            body.append(word_table(item[1], item[2]))
        elif item[0] == "image":
            body.append(word_image(item[1], item[2], item[3]))
body.append('''<w:sectPr><w:headerReference w:type="default" r:id="rIdHeader"/><w:footerReference w:type="default" r:id="rIdFooter"/>
<w:pgSz w:w="11906" w:h="16838"/><w:pgMar w:top="900" w:right="900" w:bottom="900" w:left="900" w:header="450" w:footer="450"/></w:sectPr>''')
document = f'<?xml version="1.0" encoding="UTF-8" standalone="yes"?><w:document xmlns:w="{W}" xmlns:r="{R}"><w:body>{"".join(body)}</w:body></w:document>'

styles = f'''<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<w:styles xmlns:w="{W}">
<w:docDefaults><w:rPrDefault><w:rPr><w:rFonts w:ascii="Calibri" w:hAnsi="Calibri" w:eastAsia="宋体"/><w:sz w:val="20"/><w:szCs w:val="20"/><w:lang w:val="zh-CN" w:eastAsia="zh-CN"/></w:rPr></w:rPrDefault><w:pPrDefault><w:pPr><w:spacing w:after="100" w:line="290" w:lineRule="auto"/><w:widowControl/></w:pPr></w:pPrDefault></w:docDefaults>
<w:style w:type="paragraph" w:default="1" w:styleId="Normal"><w:name w:val="Normal"/></w:style>
<w:style w:type="paragraph" w:styleId="Title"><w:name w:val="Title"/><w:basedOn w:val="Normal"/><w:pPr><w:spacing w:before="40" w:after="100"/><w:keepNext/></w:pPr><w:rPr><w:rFonts w:eastAsia="微软雅黑"/><w:b/><w:color w:val="17365D"/><w:sz w:val="36"/></w:rPr></w:style>
<w:style w:type="paragraph" w:styleId="Subtitle"><w:name w:val="Subtitle"/><w:basedOn w:val="Normal"/><w:pPr><w:keepNext/></w:pPr><w:rPr><w:color w:val="64748B"/><w:sz w:val="24"/></w:rPr></w:style>
<w:style w:type="paragraph" w:styleId="Heading1"><w:name w:val="heading 1"/><w:basedOn w:val="Normal"/><w:pPr><w:keepNext/><w:outlineLvl w:val="0"/><w:spacing w:before="100" w:after="120"/></w:pPr><w:rPr><w:rFonts w:eastAsia="微软雅黑"/><w:b/><w:color w:val="17365D"/><w:sz w:val="26"/></w:rPr></w:style>
<w:style w:type="paragraph" w:styleId="Heading2"><w:name w:val="heading 2"/><w:basedOn w:val="Normal"/><w:pPr><w:keepNext/><w:outlineLvl w:val="1"/><w:spacing w:before="100" w:after="70"/></w:pPr><w:rPr><w:rFonts w:eastAsia="微软雅黑"/><w:b/><w:color w:val="24476B"/><w:sz w:val="22"/></w:rPr></w:style>
<w:style w:type="paragraph" w:styleId="Caption"><w:name w:val="caption"/><w:basedOn w:val="Normal"/><w:rPr><w:color w:val="64748B"/><w:sz w:val="17"/></w:rPr></w:style>
</w:styles>'''
relationships = '''<?xml version="1.0" encoding="UTF-8"?>
<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">
<Relationship Id="rIdStyles" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/styles" Target="styles.xml"/>
<Relationship Id="rIdHeader" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/header" Target="header1.xml"/>
<Relationship Id="rIdFooter" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/footer" Target="footer1.xml"/>
<Relationship Id="rIdImage" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/image" Target="media/board_ctp.png"/>
</Relationships>'''
types = '''<?xml version="1.0" encoding="UTF-8"?>
<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">
<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>
<Default Extension="xml" ContentType="application/xml"/><Default Extension="png" ContentType="image/png"/>
<Override PartName="/word/document.xml" ContentType="application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml"/>
<Override PartName="/word/styles.xml" ContentType="application/vnd.openxmlformats-officedocument.wordprocessingml.styles+xml"/>
<Override PartName="/word/header1.xml" ContentType="application/vnd.openxmlformats-officedocument.wordprocessingml.header+xml"/>
<Override PartName="/word/footer1.xml" ContentType="application/vnd.openxmlformats-officedocument.wordprocessingml.footer+xml"/>
<Override PartName="/docProps/core.xml" ContentType="application/vnd.openxmlformats-package.core-properties+xml"/>
</Types>'''
rootrels = '''<?xml version="1.0" encoding="UTF-8"?>
<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">
<Relationship Id="rIdDocument" Type="http://schemas.openxmlformats.org/officeDocument/2006/relationships/officeDocument" Target="word/document.xml"/>
<Relationship Id="rIdCore" Type="http://schemas.openxmlformats.org/package/2006/relationships/metadata/core-properties" Target="docProps/core.xml"/>
</Relationships>'''
header = f'<w:hdr xmlns:w="{W}">{paragraph("HXESP3636AI0180DIC-A  |  TP芯片替换评估  |  V1.0", "Caption")}</w:hdr>'
footer = f'''<w:ftr xmlns:w="{W}"><w:p><w:pPr><w:jc w:val="center"/></w:pPr>{text_run("资料评估 / 未经样机验证    ·    第 ", size=16)}<w:fldSimple w:instr="PAGE"/>{text_run(" 页", size=16)}</w:p></w:ftr>'''
core = f'''<?xml version="1.0" encoding="UTF-8"?>
<cp:coreProperties xmlns:cp="http://schemas.openxmlformats.org/package/2006/metadata/core-properties" xmlns:dc="http://purl.org/dc/elements/1.1/" xmlns:dcterms="http://purl.org/dc/terms/" xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance">
<dc:title>{NAME}</dc:title><dc:subject>HXESP3636AI0180DIC-A硬件替代评估</dc:subject><dc:creator>Codex</dc:creator>
<dcterms:created xsi:type="dcterms:W3CDTF">{DATE}T00:00:00Z</dcterms:created><cp:revision>1</cp:revision>
</cp:coreProperties>'''
target = OUT / (NAME + ".docx")
with ZipFile(target, "w", ZIP_DEFLATED) as z:
    for name, content in {"[Content_Types].xml": types, "_rels/.rels": rootrels,
                          "word/document.xml": document, "word/styles.xml": styles,
                          "word/_rels/document.xml.rels": relationships,
                          "word/header1.xml": header, "word/footer1.xml": footer,
                          "docProps/core.xml": core}.items():
        z.writestr(name, content.encode("utf-8"))
    z.write(WORK / "board_ctp.png", "word/media/board_ctp.png")

# Check the package, every XML part, relationships, embedded image and report content.
from xml.etree import ElementTree as ET
with ZipFile(target) as z:
    assert z.testzip() is None
    for part in z.namelist():
        if part.endswith((".xml", ".rels")):
            ET.fromstring(z.read(part))
    node = ET.fromstring(z.read("word/document.xml"))
    assert len(node.findall(".//{" + W + "}tbl")) == 4
    assert len(node.findall(".//{" + W + "}br")) == 4
    all_text = "".join(t.text or "" for t in node.findall(".//{" + W + "}t"))
    for phrase in ("17路", "10路", "0.35", "不能在现有PCB上直接替换", "CST820-R", "未经"):
        if phrase != "未经":
            assert phrase in all_text, phrase
assert len(p820) == 24 and len(p816) == len(p810) == len(nets) == 20
assert p810[1] == "ID1" and p810[7] == "NC"
assert p820[18] == "VDDA" and p820[23] == "INT"
manifest = {"date": DATE, "source_directory": str(SOURCE),
            "files": [{"name": p.name, "bytes": p.stat().st_size, "sha256": hashlib.sha256(p.read_bytes()).hexdigest()}
                      for p in sorted(OUT.iterdir()) if p.is_file()]}
print(json.dumps(manifest, ensure_ascii=False, indent=2))
