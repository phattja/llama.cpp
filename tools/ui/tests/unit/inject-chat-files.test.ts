import { AttachmentType, MessageRole } from '$lib/enums';
import { ChatUploadsService } from '$lib/services/chat-uploads.service';
import type { DatabaseMessageExtra } from '$lib/types/database';
import {
	collectLastUserMessageExtras,
	extrasToChatFiles,
	injectChatFilesIntoToolArgs
} from '$lib/utils/inject-chat-files';
import { beforeEach, describe, expect, it, vi } from 'vitest';

const pngDataUrl = 'data:image/png;base64,iVBORw0KGgo=';
const pdfMime = 'application/pdf';
const pdfDataUrl = `data:${pdfMime};base64,JVBERi0=`;
const pngPath = '/tmp/llama-server-uploads/id1_scan.png';
const pdfPath = '/tmp/llama-server-uploads/id2_doc.pdf';

vi.mock('$lib/services/chat-uploads.service', () => ({
	ChatUploadsService: {
		upload: vi.fn()
	}
}));

describe('injectChatFilesIntoToolArgs', () => {
	beforeEach(() => {
		vi.mocked(ChatUploadsService.upload).mockImplementation(async (name) => {
			const path = name.endsWith('.pdf') ? pdfPath : pngPath;

			return { id: 'id', mimeType: 'application/octet-stream', name, path, size: 8 };
		});
	});

	it('injects __files__, __file__, __image__, image, and file_id for a PNG using a server path', async () => {
		const extras: DatabaseMessageExtra[] = [
			{ base64Url: pngDataUrl, name: 'scan.png', type: AttachmentType.IMAGE }
		];
		const out = await injectChatFilesIntoToolArgs({}, extras);

		expect(out.__image__).toBe(pngPath);
		expect(out.image).toBe(pngPath);
		expect(out.file_id).toBe(pngPath);
		expect(out.file).toBe(pngPath);
		expect(out.path).toBe(pngPath);
		expect(out.__file__).toMatchObject({ name: 'scan.png', path: pngPath, type: 'image', url: pngPath });
		expect(out.__files__).toHaveLength(1);
		expect(ChatUploadsService.upload).toHaveBeenCalled();
	});

	it('injects a PDF as type file with a server path', async () => {
		const extras: DatabaseMessageExtra[] = [
			{
				base64Data: 'JVBERi0=',
				content: '',
				name: 'doc.pdf',
				processedAsImages: false,
				type: AttachmentType.PDF
			}
		];
		const out = await injectChatFilesIntoToolArgs({ task: 'v1.5' }, extras);

		expect(out.image).toBe(pdfPath);
		expect(out.__file__).toMatchObject({
			mimeType: 'application/pdf',
			name: 'doc.pdf',
			path: pdfPath,
			type: 'file',
			url: pdfPath
		});
		expect(out.task).toBe('v1.5');
	});

	it('does not overwrite image or __files__ the model already set', async () => {
		const extras: DatabaseMessageExtra[] = [
			{ base64Url: pngDataUrl, name: 'scan.png', type: AttachmentType.IMAGE }
		];
		const out = await injectChatFilesIntoToolArgs(
			{ __files__: ['keep'], image: '/tmp/explicit.png' },
			extras
		);

		expect(out.image).toBe('/tmp/explicit.png');
		expect(out.__files__).toEqual(['keep']);
		expect(out.__image__).toBe(pngPath);
	});

	it('returns args unchanged when there are no file extras', async () => {
		const extras: DatabaseMessageExtra[] = [
			{ content: 'hello', name: 'notes.txt', type: AttachmentType.TEXT }
		];

		expect(await injectChatFilesIntoToolArgs({ page: '1' }, extras)).toEqual({ page: '1' });
	});

	it('falls back to a data URI when upload fails', async () => {
		vi.mocked(ChatUploadsService.upload).mockRejectedValueOnce(new Error('offline'));

		const extras: DatabaseMessageExtra[] = [
			{ base64Url: pngDataUrl, name: 'scan.png', type: AttachmentType.IMAGE }
		];
		const out = await injectChatFilesIntoToolArgs({}, extras);

		expect(out.image).toBe(pngDataUrl);
		expect(out.__file__).toMatchObject({ name: 'scan.png', url: pngDataUrl });
	});
});

describe('collectLastUserMessageExtras', () => {
	it('returns extras from the last user message', () => {
		const extras = collectLastUserMessageExtras([
			{
				extra: [{ base64Url: pngDataUrl, name: 'old.png', type: AttachmentType.IMAGE }],
				role: MessageRole.USER
			},
			{ extra: [], role: MessageRole.ASSISTANT },
			{
				extra: [
					{
						base64Data: 'JVBERi0=',
						content: '',
						name: 'latest.pdf',
						processedAsImages: false,
						type: AttachmentType.PDF
					}
				],
				role: MessageRole.USER
			}
		]);

		expect(extras).toHaveLength(1);
		expect(extras[0]).toMatchObject({ name: 'latest.pdf' });
	});
});

describe('extrasToChatFiles', () => {
	it('maps image and pdf extras and skips text', async () => {
		const files = await extrasToChatFiles([
			{ base64Url: pngDataUrl, name: 'a.png', type: AttachmentType.IMAGE },
			{ content: 'x', name: 'a.txt', type: AttachmentType.TEXT },
			{
				base64Data: 'JVBERi0=',
				content: '',
				name: 'a.pdf',
				processedAsImages: false,
				type: AttachmentType.PDF
			}
		]);

		expect(files.map((f) => f.type)).toEqual(['image', 'file']);
		expect(files[1].url).toBe(pdfPath);
		expect(files[1].path).toBe(pdfPath);
	});
});
